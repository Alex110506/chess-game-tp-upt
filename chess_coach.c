/*
 * chess_coach.c — implementare client AI Chess Coach.
 *
 * Folosim libcurl pe un fir pthread separat pentru a tine bucla principala
 * de UI (60Hz) nerelativ-blocata. Endpoint-ul backend este
 *
 *     POST {CHESS_BACKEND_URL}/api/v1/coach/chat/stream
 *
 * si emite Server-Sent Events cu urmatoarele forme:
 *
 *     data: {"v": "<token>"}        -> bucata de text din raspuns
 *     data: {"t": "<tool_name>"}    -> coach-ul a invocat un tool (info)
 *     data: {"e": "<mesaj>"}        -> eroare la nivel de aplicatie
 *     data: [DONE]                  -> sfarsit de flux
 *
 * Pe masura ce token-urile "v" sosesc, le concatenam la ultimul mesaj din
 * lista (cel asistant gol pe care l-am pus la trimitere) pentru efectul
 * de "typing".
 */

#include "chess_coach.h"
#include "chess_auth.h"   /* auth_server_url(), gAuth pentru token         */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <curl/curl.h>


/* ─────────────────────────────────────────────────────────────────────────
 * Stare partajata. Toata starea de mai jos este protejata de g_lock, in
 * afara de g_session_url (read-only dupa init) si g_curl_inited (atomic
 * la nivel de aplicatie).
 * ────────────────────────────────────────────────────────────────────── */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static ChatMsg     g_messages[COACH_MAX_MSG];
static int         g_msg_count = 0;
static CoachStatus g_status    = COACH_IDLE;
static char        g_error[COACH_ERR_MAX];

/* Identificator de sesiune trimis catre server (informatie de log). */
static char g_session_id[24];

/* Bool atomic-ish (semnal de oprire pt fir). pthread oferă mutex; il folosim. */
static bool g_shutdown = false;

/* Firul worker activ. Cand g_worker_busy este true, g_worker este valid. */
static pthread_t g_worker;
static bool      g_worker_busy = false;

/* libcurl global init (apelat o singura data). */
static bool g_curl_inited = false;


/* ─────────────────────────────────────────────────────────────────────────
 * Helpers — manipulare mesaje (apelate sub g_lock).
 * ────────────────────────────────────────────────────────────────────── */

static void push_message(CoachRole role, const char *text)
{
    if (g_msg_count >= COACH_MAX_MSG) {
        /* sertăm: scoatem cel mai vechi mesaj, decalam restul. */
        memmove(&g_messages[0], &g_messages[1],
                sizeof(ChatMsg) * (COACH_MAX_MSG - 1));
        g_msg_count = COACH_MAX_MSG - 1;
    }
    ChatMsg *m = &g_messages[g_msg_count++];
    m->role = role;
    m->len  = 0;
    m->content[0] = '\0';
    if (text && *text) {
        int n = (int)strlen(text);
        if (n > COACH_MSG_CAP - 1) n = COACH_MSG_CAP - 1;
        memcpy(m->content, text, (size_t)n);
        m->content[n] = '\0';
        m->len = n;
    }
}

static void append_to_last_assistant(const char *delta, int delta_len)
{
    if (g_msg_count <= 0) return;
    ChatMsg *m = &g_messages[g_msg_count - 1];
    if (m->role != COACH_ROLE_COACH) return;
    int space = COACH_MSG_CAP - 1 - m->len;
    if (space <= 0 || delta_len <= 0) return;
    if (delta_len > space) delta_len = space;
    memcpy(m->content + m->len, delta, (size_t)delta_len);
    m->len += delta_len;
    m->content[m->len] = '\0';
}


/* ─────────────────────────────────────────────────────────────────────────
 * JSON — escape minimal pentru construirea payload-ului si decodare
 * minimala a sirurilor primite in SSE.
 * ────────────────────────────────────────────────────────────────────── */

/* Concateneaza la *dst o varianta JSON-escapata a src. Avanseaza *dst.
 * Returneaza true daca a incaput intreg. */
static bool json_append_escaped(char **dst, char *end, const char *src)
{
    if (!src) src = "";
    char *p = *dst;
    while (*src) {
        unsigned char c = (unsigned char)*src++;
        if (p + 6 >= end) { *dst = p; return false; }
        switch (c) {
            case '"':  *p++ = '\\'; *p++ = '"';  break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\n': *p++ = '\\'; *p++ = 'n';  break;
            case '\r': *p++ = '\\'; *p++ = 'r';  break;
            case '\t': *p++ = '\\'; *p++ = 't';  break;
            case '\b': *p++ = '\\'; *p++ = 'b';  break;
            case '\f': *p++ = '\\'; *p++ = 'f';  break;
            default:
                if (c < 0x20) {
                    /* control char rar: emite \u00XX */
                    p += snprintf(p, (size_t)(end - p), "\\u%04x", c);
                } else {
                    *p++ = (char)c;
                }
        }
    }
    *dst = p;
    return true;
}

/* Decodeaza partea de valoare a unui string JSON (fara ghilimelele de
 * inchidere) intr-un buffer destinatie. Returneaza nr de bytes scrisi.
 * src trebuie sa pointeze imediat dupa ghilimeaua de deschidere. Se
 * opreste la ghilimeaua nepatchuita, fara a o consuma. */
static int json_decode_string_into(const char *src, char *dst, int dst_cap)
{
    int i = 0;
    while (*src && *src != '"' && i < dst_cap - 1) {
        if (*src == '\\' && src[1]) {
            char esc = src[1];
            src += 2;
            switch (esc) {
                case '"':  dst[i++] = '"';  break;
                case '\\': dst[i++] = '\\'; break;
                case '/':  dst[i++] = '/';  break;
                case 'n':  dst[i++] = '\n'; break;
                case 'r':  dst[i++] = '\r'; break;
                case 't':  dst[i++] = '\t'; break;
                case 'b':  dst[i++] = '\b'; break;
                case 'f':  dst[i++] = '\f'; break;
                case 'u': {
                    /* sequenta \uXXXX — decodam codepoint BMP in UTF-8 */
                    if (!src[0] || !src[1] || !src[2] || !src[3]) break;
                    unsigned cp = 0;
                    for (int k = 0; k < 4; k++) {
                        char hc = src[k];
                        unsigned v = 0;
                        if (hc >= '0' && hc <= '9') v = (unsigned)(hc - '0');
                        else if (hc >= 'a' && hc <= 'f') v = (unsigned)(hc - 'a' + 10);
                        else if (hc >= 'A' && hc <= 'F') v = (unsigned)(hc - 'A' + 10);
                        cp = (cp << 4) | v;
                    }
                    src += 4;
                    if (cp < 0x80) {
                        if (i < dst_cap - 1) dst[i++] = (char)cp;
                    } else if (cp < 0x800) {
                        if (i < dst_cap - 2) {
                            dst[i++] = (char)(0xC0 | (cp >> 6));
                            dst[i++] = (char)(0x80 | (cp & 0x3F));
                        }
                    } else {
                        if (i < dst_cap - 3) {
                            dst[i++] = (char)(0xE0 | (cp >> 12));
                            dst[i++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            dst[i++] = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default: dst[i++] = esc; break;
            }
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
    return i;
}

/* Cauta '"key":"value"' si decodeaza valoarea in dst. Returneaza true daca
 * a fost gasita cheia. */
static bool json_get_str(const char *src, const char *key, char *dst, int dst_cap)
{
    if (dst_cap <= 0 || !src) return false;
    dst[0] = '\0';
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(src, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (*p != '"') return false;
    p++;
    json_decode_string_into(p, dst, dst_cap);
    return true;
}


/* ─────────────────────────────────────────────────────────────────────────
 * Payload construction.
 * Construim ceva de forma:
 *   {"fen":"...", "messages":[{"role":"user","content":"..."}, ...],
 *    "last_move_san":"...","side_to_move":"...","difficulty":"...",
 *    "session_id":"..."}
 * ────────────────────────────────────────────────────────────────────── */

typedef struct {
    char  fen[160];
    char  last_move[16];
    char  side[8];
    char  difficulty[16];
} CoachJobInputs;

/* Construieste payload-ul JSON intr-un buffer alocat dinamic. Caller-ul
 * elibereaza. Apelata SUB g_lock (pentru a accesa g_messages). Returneaza
 * NULL la out-of-memory sau daca depasim limita absoluta. */
static char *build_payload(const CoachJobInputs *in)
{
    /* Buffer generos — ~1 MB e mai mult decat suficient pentru 64 mesaje
     * a cate 4 KB fiecare, plus campurile auxiliare si escapele. */
    const size_t cap = 1024 * 1024;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    char *p   = buf;
    char *end = buf + cap;

    #define APPEND_LIT(lit) do { \
        size_t _ll = sizeof(lit) - 1; \
        if ((size_t)(end - p) <= _ll) { free(buf); return NULL; } \
        memcpy(p, lit, _ll); p += _ll; \
    } while (0)

    APPEND_LIT("{\"fen\":\"");
    if (!json_append_escaped(&p, end, in->fen)) { free(buf); return NULL; }
    APPEND_LIT("\",\"messages\":[");

    /* Excludem ultimul mesaj asistent gol (placeholder de typing). */
    int send_count = g_msg_count;
    if (send_count > 0 && g_messages[send_count - 1].role == COACH_ROLE_COACH
        && g_messages[send_count - 1].len == 0)
        send_count--;

    for (int i = 0; i < send_count; i++) {
        if (i > 0) APPEND_LIT(",");
        const char *role = (g_messages[i].role == COACH_ROLE_USER)
                           ? "user" : "assistant";
        APPEND_LIT("{\"role\":\"");
        size_t rl = strlen(role);
        if ((size_t)(end - p) <= rl) { free(buf); return NULL; }
        memcpy(p, role, rl); p += rl;
        APPEND_LIT("\",\"content\":\"");
        if (!json_append_escaped(&p, end, g_messages[i].content)) {
            free(buf); return NULL;
        }
        APPEND_LIT("\"}");
    }
    APPEND_LIT("]");

    if (in->last_move[0]) {
        APPEND_LIT(",\"last_move_san\":\"");
        if (!json_append_escaped(&p, end, in->last_move)) { free(buf); return NULL; }
        APPEND_LIT("\"");
    }
    if (in->side[0]) {
        APPEND_LIT(",\"side_to_move\":\"");
        if (!json_append_escaped(&p, end, in->side)) { free(buf); return NULL; }
        APPEND_LIT("\"");
    }
    if (in->difficulty[0]) {
        APPEND_LIT(",\"difficulty\":\"");
        if (!json_append_escaped(&p, end, in->difficulty)) { free(buf); return NULL; }
        APPEND_LIT("\"");
    }
    if (g_session_id[0]) {
        APPEND_LIT(",\"session_id\":\"");
        if (!json_append_escaped(&p, end, g_session_id)) { free(buf); return NULL; }
        APPEND_LIT("\"");
    }
    APPEND_LIT("}");

    if (p >= end) { free(buf); return NULL; }
    *p = '\0';
    return buf;

    #undef APPEND_LIT
}


/* ─────────────────────────────────────────────────────────────────────────
 * SSE parser — invocat din callback-ul de scriere libcurl.
 *
 * Acumulam input intr-un buffer de linie, procesam fiecare linie completa
 * (terminata cu '\n'), si pentru liniile "data: ..." extragem evenimentul.
 * ────────────────────────────────────────────────────────────────────── */

typedef struct {
    char  line[8192];
    int   line_len;
    bool  done;          /* am vazut "data: [DONE]" */
    bool  saw_first_token; /* prima oara cand setam STREAMING dupa THINKING */
} SseParserState;

/* Proceseaza o linie completa (fara terminatorul de linie). */
static void sse_handle_line(SseParserState *ss, const char *line)
{
    /* SSE: ignoram linii blank si comentarii. */
    if (line[0] == '\0' || line[0] == ':') return;

    const char *data = NULL;
    if (strncmp(line, "data:", 5) == 0) {
        data = line + 5;
        while (*data == ' ') data++;
    } else {
        /* alte campuri SSE (event:, id:, retry:) — le ignoram. */
        return;
    }

    if (strcmp(data, "[DONE]") == 0) {
        ss->done = true;
        return;
    }

    /* Asteptam {"v":"..."}, {"e":"..."} sau {"t":"..."}. */
    char val[COACH_MSG_CAP];
    if (json_get_str(data, "v", val, (int)sizeof(val))) {
        int n = (int)strlen(val);
        if (n > 0) {
            pthread_mutex_lock(&g_lock);
            if (!ss->saw_first_token) {
                g_status = COACH_STREAMING;
                ss->saw_first_token = true;
            }
            append_to_last_assistant(val, n);
            pthread_mutex_unlock(&g_lock);
        }
        return;
    }
    if (json_get_str(data, "e", val, (int)sizeof(val))) {
        pthread_mutex_lock(&g_lock);
        snprintf(g_error, sizeof(g_error), "%s", val);
        g_status = COACH_ERROR;
        pthread_mutex_unlock(&g_lock);
        return;
    }
    /* "t" (tool name) este informativ; nu il afisam in UI in mod special. */
}

static size_t sse_write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
    SseParserState *ss = (SseParserState *)ud;
    size_t total = size * nmemb;
    const char *src = (const char *)ptr;

    for (size_t i = 0; i < total; i++) {
        char c = src[i];
        if (c == '\r') continue;          /* normalizam CRLF -> LF */
        if (c == '\n') {
            ss->line[ss->line_len] = '\0';
            sse_handle_line(ss, ss->line);
            ss->line_len = 0;
            if (ss->done) {
                /* nu intrerupem brutal — lasam transfer-ul sa se inchida
                 * curat; daca server-ul mai trimite bytes, vor fi ignorati. */
            }
        } else {
            if (ss->line_len < (int)sizeof(ss->line) - 1) {
                ss->line[ss->line_len++] = c;
            } else {
                /* linie prea lunga — flush si reset */
                ss->line[ss->line_len] = '\0';
                sse_handle_line(ss, ss->line);
                ss->line_len = 0;
            }
        }
    }
    return total;
}

/* Callback de progres — verifica flag-ul de shutdown si intrerupe transfer-ul
 * curat daca aplicatia se inchide. */
static int sse_progress_cb(void *clientp,
                           curl_off_t dltotal, curl_off_t dlnow,
                           curl_off_t ultotal, curl_off_t ulnow)
{
    (void)clientp; (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    pthread_mutex_lock(&g_lock);
    bool stop = g_shutdown;
    pthread_mutex_unlock(&g_lock);
    return stop ? 1 : 0; /* != 0 anuleaza transfer-ul */
}


/* ─────────────────────────────────────────────────────────────────────────
 * Firul worker.
 * ────────────────────────────────────────────────────────────────────── */

typedef struct {
    char           *payload;       /* eliberat de fir                       */
    char            url[512];
    char            bearer[160];   /* "Bearer xxx" sau "" daca nu suntem logati */
} CoachWorkerArgs;

static void *coach_worker(void *raw_args)
{
    CoachWorkerArgs *args = (CoachWorkerArgs *)raw_args;
    if (!args || !args->payload) {
        if (args) free(args->payload);
        free(args);
        return NULL;
    }

    CURL *c = curl_easy_init();
    if (!c) {
        pthread_mutex_lock(&g_lock);
        snprintf(g_error, sizeof(g_error), "curl init failed");
        g_status = COACH_ERROR;
        pthread_mutex_unlock(&g_lock);
        free(args->payload);
        free(args);
        pthread_mutex_lock(&g_lock);
        g_worker_busy = false;
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    if (args->bearer[0]) {
        char buf[200];
        snprintf(buf, sizeof(buf), "Authorization: %s", args->bearer);
        headers = curl_slist_append(headers, buf);
    }

    SseParserState ss = {0};
    ss.line_len = 0;
    ss.done = false;
    ss.saw_first_token = false;

    curl_easy_setopt(c, CURLOPT_URL,            args->url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(c, CURLOPT_POST,           1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,     args->payload);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE,  (long)strlen(args->payload));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  sse_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &ss);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS,     0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, sse_progress_cb);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL,       1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    /* fara CURLOPT_TIMEOUT — fluxul SSE poate dura. */
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE,     2048L);
    curl_easy_setopt(c, CURLOPT_TCP_NODELAY,    1L);

    CURLcode rc = curl_easy_perform(c);
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);

    /* Daca s-a inchis fluxul fara [DONE] si fara eroare aplicativa,
     * tratam ca eroare HTTP/retea. */
    pthread_mutex_lock(&g_lock);
    if (rc == CURLE_ABORTED_BY_CALLBACK) {
        /* shutdown — nu marcam eroare. */
        if (g_status != COACH_ERROR) g_status = COACH_IDLE;
    } else if (rc != CURLE_OK) {
        snprintf(g_error, sizeof(g_error), "network: %s", curl_easy_strerror(rc));
        g_status = COACH_ERROR;
    } else if (http_code >= 400) {
        if (g_status != COACH_ERROR) {
            if (http_code == 401)      snprintf(g_error, sizeof(g_error), "session expired — please log in");
            else if (http_code == 402) snprintf(g_error, sizeof(g_error), "AI Coach requires a Pro subscription");
            else if (http_code == 429) snprintf(g_error, sizeof(g_error), "rate limited — try again shortly");
            else if (http_code >= 500) snprintf(g_error, sizeof(g_error), "server error (%ld)", http_code);
            else                       snprintf(g_error, sizeof(g_error), "request failed (%ld)", http_code);
            g_status = COACH_ERROR;
        }
    } else if (!ss.done && g_status != COACH_ERROR) {
        snprintf(g_error, sizeof(g_error), "stream ended unexpectedly");
        g_status = COACH_ERROR;
    } else if (g_status != COACH_ERROR) {
        g_status = COACH_IDLE;
    }
    g_worker_busy = false;
    pthread_mutex_unlock(&g_lock);

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    free(args->payload);
    free(args);
    return NULL;
}


/* ─────────────────────────────────────────────────────────────────────────
 * API public.
 * ────────────────────────────────────────────────────────────────────── */

void coach_init(void)
{
    if (!g_curl_inited) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        g_curl_inited = true;
    }
    pthread_mutex_lock(&g_lock);
    g_msg_count = 0;
    g_status    = COACH_IDLE;
    g_error[0]  = '\0';
    g_shutdown  = false;
    snprintf(g_session_id, sizeof(g_session_id), "gui-%ld", (long)time(NULL));
    pthread_mutex_unlock(&g_lock);
}

void coach_cleanup(void)
{
    /* semnalam shutdown si asteptam firul activ (daca exista) */
    pthread_mutex_lock(&g_lock);
    g_shutdown = true;
    bool need_join = g_worker_busy;
    pthread_mutex_unlock(&g_lock);
    if (need_join) {
        pthread_join(g_worker, NULL);
    }
    /* nu apelam curl_global_cleanup() aici pentru ca chess_auth.c o
     * gestioneaza si nu vrem un double-free. */
}

void coach_reset(void)
{
    /* daca un fir e in lucru, semnalam-l sa se opreasca si asteptam. */
    pthread_mutex_lock(&g_lock);
    bool busy = g_worker_busy;
    if (busy) g_shutdown = true;
    pthread_mutex_unlock(&g_lock);
    if (busy) {
        pthread_join(g_worker, NULL);
        pthread_mutex_lock(&g_lock);
        g_shutdown = false;
        pthread_mutex_unlock(&g_lock);
    }
    pthread_mutex_lock(&g_lock);
    g_msg_count = 0;
    g_status    = COACH_IDLE;
    g_error[0]  = '\0';
    pthread_mutex_unlock(&g_lock);
}

CoachStatus coach_status(void)
{
    pthread_mutex_lock(&g_lock);
    CoachStatus s = g_status;
    pthread_mutex_unlock(&g_lock);
    return s;
}

const char *coach_last_error(void)
{
    static char snap[COACH_ERR_MAX];
    pthread_mutex_lock(&g_lock);
    snprintf(snap, sizeof(snap), "%s", g_error);
    pthread_mutex_unlock(&g_lock);
    return snap;
}

int coach_snapshot(ChatMsg *dst, int dst_max)
{
    if (!dst || dst_max <= 0) return 0;
    pthread_mutex_lock(&g_lock);
    int n = g_msg_count;
    if (n > dst_max) n = dst_max;
    memcpy(dst, g_messages, sizeof(ChatMsg) * (size_t)n);
    pthread_mutex_unlock(&g_lock);
    return n;
}

bool coach_send(const char *user_text,
                const char *fen,
                const char *last_move,
                const char *side_to_move,
                const char *difficulty)
{
    if (!user_text || !*user_text || !fen || !*fen) return false;

    /* Sub lock: verificam ca nu este alta cerere, adaugam mesajul user-ului
     * si un mesaj asistent gol care va fi populat in timpul stream-ului. */
    pthread_mutex_lock(&g_lock);
    if (g_worker_busy) {
        pthread_mutex_unlock(&g_lock);
        return false;
    }
    push_message(COACH_ROLE_USER, user_text);
    push_message(COACH_ROLE_COACH, "");        /* placeholder pentru typing */

    CoachJobInputs in = {0};
    snprintf(in.fen,         sizeof(in.fen),         "%s", fen);
    if (last_move)     snprintf(in.last_move,  sizeof(in.last_move),  "%s", last_move);
    if (side_to_move)  snprintf(in.side,       sizeof(in.side),       "%s", side_to_move);
    if (difficulty)    snprintf(in.difficulty, sizeof(in.difficulty), "%s", difficulty);

    char *payload = build_payload(&in);
    if (!payload) {
        /* rollback: scoatem cele doua mesaje pe care le-am pus. */
        if (g_msg_count >= 2) g_msg_count -= 2;
        snprintf(g_error, sizeof(g_error), "out of memory");
        g_status = COACH_ERROR;
        pthread_mutex_unlock(&g_lock);
        return false;
    }

    g_status    = COACH_THINKING;
    g_error[0]  = '\0';
    g_shutdown  = false;
    g_worker_busy = true;
    pthread_mutex_unlock(&g_lock);

    /* Construim argumentele pentru fir (in afara lock-ului). */
    CoachWorkerArgs *args = (CoachWorkerArgs *)calloc(1, sizeof(*args));
    if (!args) {
        free(payload);
        pthread_mutex_lock(&g_lock);
        g_worker_busy = false;
        snprintf(g_error, sizeof(g_error), "out of memory");
        g_status = COACH_ERROR;
        pthread_mutex_unlock(&g_lock);
        return false;
    }
    args->payload = payload;
    snprintf(args->url, sizeof(args->url),
             "%s/api/v1/coach/chat/stream", auth_server_url());
    if (gAuth.logged_in && gAuth.token[0]) {
        snprintf(args->bearer, sizeof(args->bearer), "Bearer %s", gAuth.token);
    } else {
        args->bearer[0] = '\0';
    }

    if (pthread_create(&g_worker, NULL, coach_worker, args) != 0) {
        free(payload);
        free(args);
        pthread_mutex_lock(&g_lock);
        g_worker_busy = false;
        snprintf(g_error, sizeof(g_error), "could not start worker thread");
        g_status = COACH_ERROR;
        pthread_mutex_unlock(&g_lock);
        return false;
    }
    return true;
}

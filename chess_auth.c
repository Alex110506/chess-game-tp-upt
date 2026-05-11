/*
 * chess_auth.c — client HTTP (libcurl) pentru endpoint-urile de autentificare
 * si ranking ale backend-ului FastAPI.
 *
 * Parsarea JSON este intentionat minimalista (la fel ca in chess_net.c) — nu
 * folosim o biblioteca de JSON pentru ca raspunsurile sunt mici si controlate
 * de noi.
 */

#include "chess_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>

AuthState gAuth = {0};

#define DEFAULT_SESSION_PATH "chess_session.txt"
#define RESP_CAP             4096
#define DEFAULT_URL          "http://127.0.0.1:8765"

static char g_server_url[256] = {0};
static char g_session_file[256] = {0};
static bool g_curl_inited = false;

// returneaza calea fisierului de sesiune; permite izolare per-proces
// prin variabila de mediu CHESS_SESSION_FILE (utila cand rulezi mai multe
// GUI-uri pe aceeasi masina cu conturi diferite).
static const char *session_file_path(void)
{
    if (g_session_file[0]) return g_session_file;
    const char *env = getenv("CHESS_SESSION_FILE");
    if (env && *env) snprintf(g_session_file, sizeof(g_session_file), "%s", env);
    else             snprintf(g_session_file, sizeof(g_session_file), "%s", DEFAULT_SESSION_PATH);
    return g_session_file;
}

// ---------------------------------------------------------------------------
// URL backend
// ---------------------------------------------------------------------------

const char *auth_server_url(void)
{
    if (g_server_url[0]) return g_server_url;
    const char *env = getenv("CHESS_BACKEND_URL");
    if (env && *env) {
        snprintf(g_server_url, sizeof(g_server_url), "%s", env);
    } else {
        snprintf(g_server_url, sizeof(g_server_url), "%s", DEFAULT_URL);
    }
    // strip trailing slash
    int n = (int)strlen(g_server_url);
    while (n > 0 && g_server_url[n - 1] == '/') {
        g_server_url[--n] = '\0';
    }
    return g_server_url;
}

// ---------------------------------------------------------------------------
// libcurl helpers
// ---------------------------------------------------------------------------

void auth_init(void)
{
    if (!g_curl_inited) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        g_curl_inited = true;
    }
}

void auth_cleanup(void)
{
    if (g_curl_inited) {
        curl_global_cleanup();
        g_curl_inited = false;
    }
}

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} RespBuf;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t add = size * nmemb;
    RespBuf *r = (RespBuf *)userp;
    if (r->len + add + 1 > r->cap) add = (r->cap > r->len + 1) ? (r->cap - r->len - 1) : 0;
    if (add > 0) {
        memcpy(r->buf + r->len, contents, add);
        r->len += add;
        r->buf[r->len] = '\0';
    }
    return size * nmemb;  // pretindem ca am consumat tot, ca sa nu intrerupem transferul
}

// extrage un sir din JSON: "key":"value"
static int json_get_str(const char *src, const char *key, char *dst, int dst_sz)
{
    if (dst_sz <= 0 || !src) return 0;
    dst[0] = '\0';
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(src, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < dst_sz - 1) {
        if (*p == '\\' && p[1]) { dst[i++] = p[1]; p += 2; }
        else                    { dst[i++] = *p++; }
    }
    dst[i] = '\0';
    return 1;
}

// extrage un intreg din JSON: "key": 123
static int json_get_int(const char *src, const char *key, int *out)
{
    if (!src || !out) return 0;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(src, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (!(*p == '-' || (*p >= '0' && *p <= '9'))) return 0;
    *out = (int)strtol(p, NULL, 10);
    return 1;
}

// extrage un mesaj de eroare in stilul FastAPI: {"detail":"..."}
static void parse_error_detail(const char *body, char *err, int err_sz)
{
    if (!err || err_sz <= 0) return;
    if (!body || !*body) { snprintf(err, err_sz, "no response from server"); return; }
    char detail[256] = {0};
    if (json_get_str(body, "detail", detail, sizeof(detail)) && detail[0]) {
        snprintf(err, err_sz, "%s", detail);
    } else {
        snprintf(err, err_sz, "request failed (%.*s)", err_sz - 32, body);
    }
}

// efectueaza un POST JSON sau GET. method: "POST" sau "GET". body poate fi NULL.
// authorization poate fi NULL (fara header) sau un token Bearer.
// returneaza codul HTTP (0 daca a esuat conexiunea).
static long http_request(const char *method, const char *path,
                         const char *body,
                         const char *bearer,
                         RespBuf *resp)
{
    auth_init();

    CURL *c = curl_easy_init();
    if (!c) return 0;

    char url[512];
    snprintf(url, sizeof(url), "%s%s", auth_server_url(), path);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char authbuf[160];
    if (bearer && *bearer) {
        snprintf(authbuf, sizeof(authbuf), "Authorization: Bearer %s", bearer);
        headers = curl_slist_append(headers, authbuf);
    }

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, resp);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

    if (!strcmp(method, "POST")) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body ? body : "");
    } else if (!strcmp(method, "GET")) {
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    }

    CURLcode rc = curl_easy_perform(c);
    long http_code = 0;
    if (rc == CURLE_OK) {
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    return http_code;
}

// escape JSON minimal (doar " si \) — folosit pentru a construi siruri JSON sigure
static void json_escape(const char *src, char *dst, int dst_sz)
{
    int i = 0;
    if (!src) src = "";
    for (; *src && i < dst_sz - 2; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') {
            if (i >= dst_sz - 3) break;
            dst[i++] = '\\';
            dst[i++] = (char)c;
        } else if (c < 0x20) {
            // skip control chars
            continue;
        } else {
            dst[i++] = (char)c;
        }
    }
    dst[i] = '\0';
}

// ---------------------------------------------------------------------------
// extragere profil din corpul raspunsului
// ---------------------------------------------------------------------------

static void apply_profile(const char *body)
{
    char uname[AUTH_USERNAME_MAX] = {0};
    if (json_get_str(body, "username", uname, sizeof(uname)) && uname[0]) {
        snprintf(gAuth.username, sizeof(gAuth.username), "%s", uname);
    }
    int v = 0;
    if (json_get_int(body, "wins",   &v)) gAuth.wins   = v;
    if (json_get_int(body, "losses", &v)) gAuth.losses = v;
    if (json_get_int(body, "ties",   &v)) gAuth.ties   = v;
    if (json_get_int(body, "rank",   &v)) gAuth.rank   = v;
}

// ---------------------------------------------------------------------------
// API public
// ---------------------------------------------------------------------------

int auth_register(const char *username, const char *password,
                  char *err_buf, int err_sz)
{
    char ubuf[64], pbuf[128], body[256];
    json_escape(username, ubuf, sizeof(ubuf));
    json_escape(password, pbuf, sizeof(pbuf));
    snprintf(body, sizeof(body), "{\"username\":\"%s\",\"password\":\"%s\"}", ubuf, pbuf);

    char data[RESP_CAP];
    RespBuf r = { data, 0, sizeof(data) };
    data[0] = '\0';

    long code = http_request("POST", "/auth/register", body, NULL, &r);
    if (code == 0) {
        if (err_buf) snprintf(err_buf, err_sz, "cannot reach server (%s)", auth_server_url());
        return 0;
    }
    if (code >= 200 && code < 300) return 1;
    if (err_buf) parse_error_detail(r.buf, err_buf, err_sz);
    return 0;
}

int auth_login(const char *username, const char *password,
               char *err_buf, int err_sz)
{
    char ubuf[64], pbuf[128], body[256];
    json_escape(username, ubuf, sizeof(ubuf));
    json_escape(password, pbuf, sizeof(pbuf));
    snprintf(body, sizeof(body), "{\"username\":\"%s\",\"password\":\"%s\"}", ubuf, pbuf);

    char data[RESP_CAP];
    RespBuf r = { data, 0, sizeof(data) };
    data[0] = '\0';

    long code = http_request("POST", "/auth/login", body, NULL, &r);
    if (code == 0) {
        if (err_buf) snprintf(err_buf, err_sz, "cannot reach server (%s)", auth_server_url());
        return 0;
    }
    if (code < 200 || code >= 300) {
        if (err_buf) parse_error_detail(r.buf, err_buf, err_sz);
        return 0;
    }

    char tok[AUTH_TOKEN_MAX] = {0};
    if (!json_get_str(r.buf, "token", tok, sizeof(tok)) || !tok[0]) {
        if (err_buf) snprintf(err_buf, err_sz, "server did not return a token");
        return 0;
    }

    gAuth.logged_in = true;
    snprintf(gAuth.token, sizeof(gAuth.token), "%s", tok);
    apply_profile(r.buf);
    auth_save_session();
    return 1;
}

int auth_refresh_profile(char *err_buf, int err_sz)
{
    if (!gAuth.logged_in) {
        if (err_buf) snprintf(err_buf, err_sz, "not logged in");
        return 0;
    }
    char data[RESP_CAP];
    RespBuf r = { data, 0, sizeof(data) };
    data[0] = '\0';

    long code = http_request("GET", "/me", NULL, gAuth.token, &r);
    if (code == 0) {
        if (err_buf) snprintf(err_buf, err_sz, "cannot reach server");
        return 0;
    }
    if (code == 401) {
        // token invalid — deconectam local
        auth_clear_session();
        if (err_buf) snprintf(err_buf, err_sz, "session expired, please log in again");
        return 0;
    }
    if (code < 200 || code >= 300) {
        if (err_buf) parse_error_detail(r.buf, err_buf, err_sz);
        return 0;
    }
    apply_profile(r.buf);
    return 1;
}

void auth_logout(void)
{
    if (gAuth.logged_in && gAuth.token[0]) {
        char data[RESP_CAP];
        RespBuf r = { data, 0, sizeof(data) };
        data[0] = '\0';
        // best-effort logout pe server (ignoram orice eroare)
        http_request("POST", "/auth/logout", "{}", gAuth.token, &r);
    }
    auth_clear_session();
}

int auth_report_game(const char *result, const char *opponent,
                     char *err_buf, int err_sz)
{
    if (!gAuth.logged_in) {
        if (err_buf) snprintf(err_buf, err_sz, "not logged in");
        return 0;
    }
    if (!result || !*result) {
        if (err_buf) snprintf(err_buf, err_sz, "invalid result");
        return 0;
    }

    char body[256];
    if (opponent && *opponent) {
        char obuf[64];
        json_escape(opponent, obuf, sizeof(obuf));
        snprintf(body, sizeof(body),
                 "{\"result\":\"%s\",\"opponent\":\"%s\"}", result, obuf);
    } else {
        snprintf(body, sizeof(body), "{\"result\":\"%s\"}", result);
    }

    char data[RESP_CAP];
    RespBuf r = { data, 0, sizeof(data) };
    data[0] = '\0';

    long code = http_request("POST", "/game/report", body, gAuth.token, &r);
    if (code == 0) {
        if (err_buf) snprintf(err_buf, err_sz, "cannot reach server");
        return 0;
    }
    if (code == 401) {
        // tokenul a fost invalidat undeva (alta instanta a facut logout sau
        // re-login peste el). curatam sesiunea local ca utilizatorul sa stie.
        auth_clear_session();
        if (err_buf) snprintf(err_buf, err_sz, "session expired, please log in again");
        return 0;
    }
    if (code < 200 || code >= 300) {
        if (err_buf) parse_error_detail(r.buf, err_buf, err_sz);
        return 0;
    }
    apply_profile(r.buf);
    return 1;
}

// ---------------------------------------------------------------------------
// Persistenta sesiunii pe disk
// ---------------------------------------------------------------------------

void auth_load_session(void)
{
    FILE *f = fopen(session_file_path(), "r");
    if (!f) return;
    char uname[AUTH_USERNAME_MAX] = {0};
    char token[AUTH_TOKEN_MAX] = {0};
    if (fscanf(f, "%31s %95s", uname, token) == 2 && uname[0] && token[0]) {
        gAuth.logged_in = true;
        snprintf(gAuth.username, sizeof(gAuth.username), "%s", uname);
        snprintf(gAuth.token, sizeof(gAuth.token), "%s", token);
    }
    fclose(f);
    // incercam sa reimprospatam profilul; daca tokenul e expirat, auth_refresh_profile
    // va goli sesiunea
    if (gAuth.logged_in) {
        auth_refresh_profile(NULL, 0);
    }
}

void auth_save_session(void)
{
    if (!gAuth.logged_in) {
        auth_clear_session();
        return;
    }
    FILE *f = fopen(session_file_path(), "w");
    if (!f) return;
    fprintf(f, "%s %s\n", gAuth.username, gAuth.token);
    fclose(f);
}

void auth_clear_session(void)
{
    gAuth.logged_in = false;
    gAuth.username[0] = '\0';
    gAuth.token[0] = '\0';
    gAuth.wins = gAuth.losses = gAuth.ties = 0;
    gAuth.rank = 0;
    remove(session_file_path());
}

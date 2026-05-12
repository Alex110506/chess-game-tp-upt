/*
 * chess_net.c — bridge catre serverul de multiplayer (FastAPI/WebSockets).
 *
 * Foloseste acelasi tipar ca integrarea Stockfish: forkam un proces copil
 * (de data aceasta scriptul Python "server/net_client.py") si schimbam
 * date prin pipe-uri. Mesajele sunt linii JSON, una per pachet.
 *
 * Parserul JSON este unul minimalist: scoatem doar campurile pe care le
 * stim (type/code/color/uci/msg). Atat clientul cat si serverul sunt
 * controlate de noi, deci nu avem nevoie de parser general.
 */

#include "chess_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <ctype.h>

#define BRIDGE_PY    "server/.venv/bin/python"
#define BRIDGE_SCRIPT "server/net_client.py"
#define DEFAULT_URL  "ws://127.0.0.1:8765/ws"

static pid_t   net_pid     = -1;
static int     net_write_fd = -1;
static int     net_read_fd  = -1;

// buffer pentru linii incomplete venite de la copil
static char    rx_buf[8192];
static int     rx_len = 0;

int net_running(void) { return net_pid > 0; }

void net_stop(void)
{
    if (net_pid <= 0) return;
    if (net_write_fd >= 0) close(net_write_fd);
    if (net_read_fd  >= 0) close(net_read_fd);
    net_write_fd = net_read_fd = -1;

    // lasam scurt timp procesului sa iasa singur, apoi il omoram
    for (int i = 0; i < 5; i++) {
        int status = 0;
        pid_t r = waitpid(net_pid, &status, WNOHANG);
        if (r == net_pid) { net_pid = -1; rx_len = 0; return; }
        usleep(50000);
    }
    kill(net_pid, SIGTERM);
    waitpid(net_pid, NULL, 0);
    net_pid = -1;
    rx_len = 0;
}

int net_start(const char *url)
{
    if (net_pid > 0) return 1; // deja pornit

    int pipe_in[2];   // parinte -> copil (stdin copil)
    int pipe_out[2];  // copil  -> parinte (stdout copil)

    if (pipe(pipe_in) < 0) return 0;
    if (pipe(pipe_out) < 0) {
        close(pipe_in[0]); close(pipe_in[1]);
        return 0;
    }

    net_pid = fork();
    if (net_pid < 0) {
        close(pipe_in[0]);  close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);
        return 0;
    }

    if (net_pid == 0) {
        // proces copil: ruleaza scriptul python
        close(pipe_in[1]);
        close(pipe_out[0]);
        dup2(pipe_in[0],  STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        // stderr ramane atasat la consola pentru depanare
        close(pipe_in[0]);
        close(pipe_out[1]);

        const char *use_url = (url && *url) ? url : DEFAULT_URL;
        execl(BRIDGE_PY, "python", BRIDGE_SCRIPT, "--url", use_url, (char *)NULL);
        // daca am ajuns aici, exec a esuat
        fprintf(stderr, "net_client bridge failed to exec %s\n", BRIDGE_PY);
        _exit(1);
    }

    // proces parinte
    close(pipe_in[0]);
    close(pipe_out[1]);
    net_write_fd = pipe_in[1];
    net_read_fd  = pipe_out[0];

    fcntl(net_read_fd, F_SETFL, O_NONBLOCK);
    rx_len = 0;
    return 1;
}

static void send_line(const char *line)
{
    if (net_write_fd < 0) return;
    ssize_t n = write(net_write_fd, line, strlen(line));
    (void)n;  // best-effort; conexiunea va fi vazuta ca inchisa daca esueaza
}

// sanitizeaza un username (alfanumeric + underscore, max 16 chars) intr-un buffer dat
static void sanitize_username(const char *src, char *dst, int dst_sz)
{
    int j = 0;
    for (int i = 0; src && src[i] && j < dst_sz - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '_') dst[j++] = (char)c;
    }
    dst[j] = '\0';
}

void net_send_create(const char *username, int time_seconds)
{
    char ubuf[24] = {0};
    sanitize_username(username, ubuf, sizeof(ubuf));
    if (time_seconds < 0) time_seconds = 0;
    char buf[128];
    if (ubuf[0])
        snprintf(buf, sizeof(buf),
                 "{\"type\":\"create\",\"username\":\"%s\",\"time\":%d}\n",
                 ubuf, time_seconds);
    else
        snprintf(buf, sizeof(buf),
                 "{\"type\":\"create\",\"time\":%d}\n", time_seconds);
    send_line(buf);
}

void net_send_join(const char *code, const char *username)
{
    char buf[128];
    // sanitizam codul: doar alfanumerice, max 4 chars
    char clean[8] = {0};
    int j = 0;
    for (int i = 0; code && code[i] && j < 4; i++) {
        unsigned char c = (unsigned char)code[i];
        if (isalnum(c)) clean[j++] = (char)toupper(c);
    }
    char ubuf[24] = {0};
    sanitize_username(username, ubuf, sizeof(ubuf));
    if (ubuf[0])
        snprintf(buf, sizeof(buf),
                 "{\"type\":\"join\",\"code\":\"%s\",\"username\":\"%s\"}\n", clean, ubuf);
    else
        snprintf(buf, sizeof(buf), "{\"type\":\"join\",\"code\":\"%s\"}\n", clean);
    send_line(buf);
}

void net_send_move(const char *uci)
{
    char buf[64];
    char clean[8] = {0};
    int j = 0;
    for (int i = 0; uci && uci[i] && j < 6; i++) {
        unsigned char c = (unsigned char)uci[i];
        if (isalnum(c)) clean[j++] = (char)tolower(c);
    }
    snprintf(buf, sizeof(buf), "{\"type\":\"move\",\"uci\":\"%s\"}\n", clean);
    send_line(buf);
}

void net_send_resign(void)
{
    send_line("{\"type\":\"resign\"}\n");
}

// extrage valoarea unui camp string ("key":"...") din 'src' in 'dst' (max dst_sz-1)
// returneaza 1 daca a fost gasit
static int json_get_str(const char *src, const char *key, char *dst, int dst_sz)
{
    if (dst_sz <= 0) return 0;
    dst[0] = '\0';

    // construieste tiparul: "key"
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(src, pat);
    if (!p) return 0;
    p += strlen(pat);
    // sare peste spatii albe si ':'
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < dst_sz - 1) {
        // suporta backslash escapes simple
        if (*p == '\\' && p[1]) {
            dst[i++] = p[1];
            p += 2;
        } else {
            dst[i++] = *p++;
        }
    }
    dst[i] = '\0';
    return 1;
}

// transforma un sir tip in valoarea NetMsgType
static NetMsgType type_from_str(const char *t)
{
    if (!strcmp(t, "connected"))      return NM_CONNECTED;
    if (!strcmp(t, "created"))        return NM_CREATED;
    if (!strcmp(t, "joined"))         return NM_JOINED;
    if (!strcmp(t, "start"))          return NM_START;
    if (!strcmp(t, "move"))           return NM_MOVE;
    if (!strcmp(t, "opponent_left"))  return NM_OPP_LEFT;
    if (!strcmp(t, "resign"))         return NM_RESIGN;
    if (!strcmp(t, "error"))          return NM_ERROR;
    if (!strcmp(t, "closed"))         return NM_CLOSED;
    return NM_NONE;
}

// transforma o linie completa (terminata cu '\0') in NetMsg
static int parse_line(const char *line, NetMsg *out)
{
    char tbuf[32] = {0};
    if (!json_get_str(line, "type", tbuf, sizeof(tbuf))) return 0;
    memset(out, 0, sizeof(*out));
    out->type = type_from_str(tbuf);
    if (out->type == NM_NONE) return 0;

    json_get_str(line, "code",     out->code,     sizeof(out->code));
    json_get_str(line, "color",    out->color,    sizeof(out->color));
    json_get_str(line, "uci",      out->uci,      sizeof(out->uci));
    json_get_str(line, "msg",      out->msg,      sizeof(out->msg));
    json_get_str(line, "opponent", out->opponent, sizeof(out->opponent));

    // parseaza timpul (intreg, "time":300)
    const char *tp = strstr(line, "\"time\"");
    if (tp) {
        tp += 6;
        while (*tp == ' ' || *tp == '\t' || *tp == ':') tp++;
        if (*tp == '-' || (*tp >= '0' && *tp <= '9')) {
            out->time_seconds = (int)strtol(tp, NULL, 10);
        }
    }
    return 1;
}

int net_poll(NetMsg *out)
{
    if (!out) return 0;
    if (net_read_fd < 0) return 0;

    // citeste cat se poate fara sa blocam
    if (rx_len < (int)sizeof(rx_buf) - 1) {
        ssize_t n = read(net_read_fd, rx_buf + rx_len,
                         sizeof(rx_buf) - 1 - rx_len);
        if (n > 0) rx_len += (int)n;
        else if (n == 0) {
            // EOF: copilul s-a inchis. raporteaza un mesaj de inchidere o singura data
            if (net_pid > 0) {
                memset(out, 0, sizeof(*out));
                out->type = NM_CLOSED;
                net_stop();
                return 1;
            }
            return 0;
        }
        // n < 0 cu EAGAIN -> nimic disponibil acum, e ok
    }

    // cauta o linie completa in buffer
    rx_buf[rx_len] = '\0';
    char *nl = memchr(rx_buf, '\n', rx_len);
    if (!nl) return 0;

    *nl = '\0';
    int line_len = (int)(nl - rx_buf);

    int ok = parse_line(rx_buf, out);

    // gliseaza ce a ramas la inceputul buffer-ului
    int rem = rx_len - (line_len + 1);
    if (rem > 0) memmove(rx_buf, nl + 1, rem);
    rx_len = rem;

    return ok;
}

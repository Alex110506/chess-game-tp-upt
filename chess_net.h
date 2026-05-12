#ifndef CHESS_NET_H
#define CHESS_NET_H

// modul de retea pentru multiplayer online
// porneste un proces python care face puntea WebSocket <-> stdin/stdout
// si schimba mesaje JSON intr-un fir simplu, neblocant

typedef enum {
    NM_NONE = 0,
    NM_CONNECTED,    // bridge-ul s-a conectat la server
    NM_CREATED,      // gazda a creat camera, cod disponibil
    NM_JOINED,       // invitatul s-a alaturat camerei
    NM_START,        // ambii jucatori sunt prezenti, jocul poate incepe
    NM_MOVE,         // mutare a oponentului (in uci)
    NM_OPP_LEFT,     // oponentul a parasit
    NM_RESIGN,       // oponentul s-a predat
    NM_ERROR,        // eroare de la server sau bridge
    NM_CLOSED        // bridge-ul s-a inchis
} NetMsgType;

typedef struct {
    NetMsgType type;
    char code[8];      // codul camerei (4 caractere + null)
    char color[8];     // "white" / "black"
    char uci[8];       // mutare uci (ex "e2e4" sau "e7e8q")
    char msg[160];     // mesaj de eroare
    char opponent[32]; // username-ul oponentului (pentru ranking)
    int  time_seconds; // timpul de joc (in secunde, 0 = fara timer)
} NetMsg;

// porneste procesul bridge si stabileste conexiunea WS catre 'url'
// (poate fi NULL pentru valoarea implicita ws://127.0.0.1:8765/ws)
// returneaza 1 la succes, 0 la esec
int net_start(const char *url);

// inchide procesul bridge si elibereaza resursele
void net_stop(void);

// 1 daca bridge-ul ruleaza
int net_running(void);

// trimite cereri tipice catre server. 'username' poate fi NULL pentru a juca ca invitat.
// 'time_seconds' este timpul pe ceas pentru fiecare jucator (0 = fara timer).
void net_send_create(const char *username, int time_seconds);
void net_send_join(const char *code, const char *username);
void net_send_move(const char *uci);
void net_send_resign(void);

// citeste un mesaj din coada (neblocant). returneaza 1 daca s-a obtinut mesaj
int net_poll(NetMsg *out);

#endif

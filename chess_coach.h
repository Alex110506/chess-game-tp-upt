#ifndef CHESS_COACH_H
#define CHESS_COACH_H

#include <stdbool.h>

/*
 * chess_coach.h — client pentru AI Chess Coach.
 *
 * Modulul tine evidenta unei conversatii cu coach-ul, executa cereri HTTP
 * catre endpoint-ul SSE /api/v1/coach/chat/stream pe un fir secundar
 * (pthread) si parseaza fluxul SSE pe masura ce soseste, permitand efectul
 * de "typing" in interfata fara a bloca bucla principala de 60Hz.
 *
 * Toata starea partajata cu firul de UI (lista de mesaje, status, eroare)
 * este protejata de un mutex unic intern. Operatiile publice sunt sigure
 * pentru apelare din firul de UI.
 */

#define COACH_MAX_MSG    64        /* numar maxim de mesaje in conversatie  */
#define COACH_MSG_CAP    4096      /* dimensiune maxima a unui mesaj (bytes) */
#define COACH_INPUT_MAX  512       /* dimensiune maxima a campului de input */
#define COACH_ERR_MAX    256       /* dimensiune maxima a mesajului de eroare */

typedef enum {
    COACH_ROLE_USER  = 0,
    COACH_ROLE_COACH = 1,
} CoachRole;

typedef struct {
    CoachRole role;
    int       len;                       /* lungimea utilizata din content   */
    char      content[COACH_MSG_CAP];    /* zero-terminat                    */
} ChatMsg;

typedef enum {
    COACH_IDLE      = 0,   /* nu exista cerere in curs                       */
    COACH_THINKING  = 1,   /* cerere trimisa, nu am primit inca niciun token */
    COACH_STREAMING = 2,   /* token-uri sosesc activ                         */
    COACH_ERROR     = 3,   /* ultima cerere a esuat (vezi coach_last_error)  */
} CoachStatus;

/* Initializare/curatare. Apelate o data, din firul de UI (gui.c). */
void coach_init(void);
void coach_cleanup(void);

/* Goleste conversatia si starea de eroare. Daca exista o cerere in curs,
 * functia asteapta finalizarea ei (timeout scurt). */
void coach_reset(void);

/* Adauga mesajul utilizatorului si porneste cererea catre backend pe un fir
 * secundar. Returneaza true daca cererea a fost programata, false daca o
 * cerere este deja in curs sau argumentele sunt invalide. Toate sirurile
 * sunt copiate intern. */
bool coach_send(const char *user_text,
                const char *fen,
                const char *last_move,
                const char *side_to_move,
                const char *difficulty);

/* Status curent. Sigur pentru citire din firul de UI. */
CoachStatus coach_status(void);

/* Returneaza ultimul mesaj de eroare (sir gol daca nu exista). Sigur pentru
 * citire din firul de UI; copiaza intr-un buffer static la apel. */
const char *coach_last_error(void);

/* Copiaza intregul istoric de mesaje in dst. Returneaza cate au fost
 * copiate (<= dst_max). Sigur pentru apel din firul de UI. */
int coach_snapshot(ChatMsg *dst, int dst_max);

#endif /* CHESS_COACH_H */

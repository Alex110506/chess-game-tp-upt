#ifndef CHESS_GUI_H
#define CHESS_GUI_H

#include "raylib.h"

// dimensiunile ferestrei principale
#define WIN_W 960
#define WIN_H 680

// starea ecranului curent din joc

typedef enum {
    SCR_HOME,
    SCR_BOTSETUP,
    SCR_MPSETUP,    // alegerea intre host si join pentru multiplayer
    SCR_MPLOBBY,    // gazda asteapta un oponent (afiseaza codul camerei)
    SCR_PUZZLESETUP, // alegerea unui puzzle "checkmate in N"
    SCR_LOGIN,      // ecran de autentificare/inregistrare
    SCR_PROFILE,    // profilul jucatorului (stats si rank)
    SCR_TIMESETUP,  // alegerea ceasului (1/5/10 min) pentru 1v1 si host MP
    SCR_GAME
} Screen;

// variabila globala care retine pe ce ecran ne aflam
extern Screen curScreen;

// functii pentru initializarea si eliberarea memoriei pentru fonturi
void init_fonts(void);
void cleanup_fonts(void);

// functii principale de desenare pentru fiecare ecran
void DrawHome(void);
void DrawBotSetup(void);
void DrawMpSetup(void);
void DrawMpLobby(void);
void DrawPuzzleSetup(void);
void DrawLogin(void);
void DrawProfile(void);
void DrawTimeSetup(void);
void DrawGame(void);

// opreste procesul motorului de sah (Stockfish) in caz de iesire
void sf_stop(void);

// opreste procesul de retea pentru multiplayer in caz de iesire
void mp_cleanup(void);

#endif // CHESS_GUI_H

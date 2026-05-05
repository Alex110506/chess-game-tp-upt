#ifndef CHESS_GUI_H
#define CHESS_GUI_H

#include "raylib.h"

// dimensiunile ferestrei principale
#define WIN_W 960
#define WIN_H 680

// starea ecranului curent din joc
typedef enum { SCR_HOME, SCR_BOTSETUP, SCR_GAME } Screen;

// variabila globala care retine pe ce ecran ne aflam
extern Screen curScreen;

// functii pentru initializarea si eliberarea memoriei pentru fonturi
void init_fonts(void);
void cleanup_fonts(void);

// functii principale de desenare pentru fiecare ecran
void DrawHome(void);
void DrawBotSetup(void);
void DrawGame(void);

// opreste procesul motorului de sah (Stockfish) in caz de iesire
void sf_stop(void);

#endif // CHESS_GUI_H

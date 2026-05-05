/*
 * chess_gui.c  —  Raylib GUI front-end for chess_logic.c
 *
 * Build:  make gui
 * Run:    ./chess_gui
 */

#include "raylib.h"
#include "chess_logic.h"
#include "chess_gui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>

#define BOARD_X 60 //spatiu pt lables
#define BOARD_Y 40 //spatiu pt lables
#define SQ 75 //dimensiune patrat
#define BOARD_PX (SQ * 8) //dimensiune tabla

//panou din dreapta
#define PANEL_X (BOARD_X + BOARD_PX + 30)
#define PANEL_Y BOARD_Y
#define PANEL_W 230
#define PANEL_H BOARD_PX

//culori
#define C_LIGHT (Color){ 240, 217, 181, 255 }
#define C_DARK (Color){ 181, 136,  99, 255 }
#define C_SEL (Color){  20, 160,  80, 200 }
#define C_HINT (Color){  20, 160,  80, 140 }
#define C_CHECK (Color){ 210,  40,  40, 210 }
#define C_BG (Color){  22,  22,  22, 255 }
#define C_PANEL (Color){  36,  36,  36, 255 }
#define C_BTN (Color){  46, 105,  56, 255 }
#define C_BTN_HOV (Color){  65, 145,  75, 255 }
#define C_BTN_DIS (Color){  55,  55,  55, 255 }
#define C_WHITE_P (Color){ 245, 240, 220, 255 }
#define C_BLACK_P (Color){  30,  30,  30, 255 }

//popupuri
typedef enum { ST_SELECT, ST_PROMOTE, ST_GAMEOVER, ST_BOT_THINKING, ST_BOT_READY, ST_HINT_THINKING } GameSt;

Screen curScreen = SCR_HOME;
static GameSt gameSt = ST_SELECT;
static double botMoveReadyTime = 0.0;  // momentul (timpul) cand mutarea botului a fost calculata

// variabile pentru patratul selectat de jucator si patratele folosite pentru 'hint'
static int selRow = -1, selCol = -1;
static int hintSrcRow = -1, hintSrcCol = -1, hintDstRow = -1, hintDstCol = -1;

// retine date despre pozitia pionului inainte si dupa promovare
static int promSrcRow, promSrcCol, promDstRow, promDstCol;

// matrice folosita ca un cache pentru a tine minte mutarile legale ale piesei selectate
static int legal[8][8];

static Font gFont;
static bool gFontHasChess = false;   /* adevarat daca fontul suporta simbolurile de sah Unicode ♔..♟ */

// setari si variabile pentru integrarea motorului Stockfish
static int botMode = 0;       // 1 = joc contra bot (botul joaca mereu cu piesele negre)
static int botDepth = 5;      // adancimea de calcul pentru Stockfish (afecteaza dificultatea)

static pid_t sf_pid = -1;     // PID-ul procesului Stockfish
static int sf_write_fd = -1;  // file descriptor pentru a trimite comenzi catre Stockfish
static int sf_read_fd = -1;   // file descriptor pentru a citi raspunsurile de la Stockfish

static char sf_buf[16384];    // buffer pentru citirea datelor
static int sf_buf_len = 0;
static char sf_bestmove[8];   // sir de caractere pentru a salva cea mai buna mutare (ex: "e2e4")

// calea catre executabilul motorului de sah
#define SF_PATH "./stockfish/src/stockfish"

// statistici pentru jucator (salvate local)
static int pWins = 0;
static int pLosses = 0;
static int pStreak = 0;
static bool statsLoaded = false;
static const char *STATS_FILE = "chess_stats.txt";

// incarca statisticile din fisier
static void load_stats(void) {
    if (statsLoaded) return;
    FILE *f = fopen(STATS_FILE, "r");
    if (f) {
        fscanf(f, "%d %d %d", &pWins, &pLosses, &pStreak);
        fclose(f);
    }
    statsLoaded = true;
}

// salveaza statisticile curente inapoi in fisier
static void save_stats(void) {
    FILE *f = fopen(STATS_FILE, "w");
    if (f) {
        fprintf(f, "%d %d %d\n", pWins, pLosses, pStreak);
        fclose(f);
    }
}

// functie care marcheaza finalul de joc si actualizeaza statisticile (doar vs bot)
static void set_game_over(void) {
    gameSt = ST_GAMEOVER;
    if (botMode) {
        bool mate = is_in_check(current_turn);
        if (mate) {
            if (current_turn == 0) {
                pLosses++;
                pStreak = 0;
            } else {
                pWins++;
                pStreak++;
            }
        } else {
            pStreak = 0;
        }
        save_stats();
    }
}

static int sf_start(void)
{
    if (sf_pid > 0) return 1; // deja pornit

    int pipe_in[2];
    int pipe_out[2];

    if (pipe(pipe_in) < 0) return 0;
    
    if (pipe(pipe_out) < 0) {
        close(pipe_in[0]);
        close(pipe_in[1]);
        return 0;
    }

    sf_pid = fork();
    if (sf_pid < 0) {
        close(pipe_in[0]);
        close(pipe_in[1]);
        close(pipe_out[0]);
        close(pipe_out[1]);
        return 0;
    }

    if (sf_pid == 0) {
        // proces copil -> stockfish
        close(pipe_in[1]);
        close(pipe_out[0]);
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_out[1], STDERR_FILENO);
        close(pipe_in[0]);
        close(pipe_out[1]);
        execl(SF_PATH, "stockfish", (char *)NULL);
        _exit(1);
    }

    // proces parinte
    close(pipe_in[0]);
    close(pipe_out[1]);
    sf_write_fd = pipe_in[1];
    sf_read_fd = pipe_out[0];

    // seteaza non-blocking pe citire
    fcntl(sf_read_fd, F_SETFL, O_NONBLOCK);

    // handshake UCI (blocat scurt)
    const char *init = "uci\nisready\n";
    write(sf_write_fd, init, strlen(init));

    // asteapta "readyok" (max ~2 secunde)
    char tmp[4096];
    for (int i = 0; i < 40; i++) {
        usleep(50000); // 50ms
        int n = (int)read(sf_read_fd, tmp, sizeof(tmp) - 1);
        if (n > 0) {
            tmp[n] = '\0';
            if (strstr(tmp, "readyok")) break;
        }
    }

    return 1;
}

void sf_stop(void)
{
    if (sf_pid <= 0) return;
    const char *cmd = "quit\n";
    write(sf_write_fd, cmd, strlen(cmd));
    close(sf_write_fd);
    close(sf_read_fd);
    sf_write_fd = sf_read_fd = -1;
    // asteapta putin sa se inchida
    usleep(100000);
    kill(sf_pid, SIGTERM);
    waitpid(sf_pid, NULL, 0); // asteapta ca procesul copil sa se inchida complet (evita zombie process)
    sf_pid = -1;
}

// solicita o noua mutare de la motorul Stockfish bazat pe starea curenta a tablei
static void sf_request_move(void)
{
    char fen[256];
    board_to_fen(fen, (int)sizeof(fen));

    char cmd[512];
    // transmitem pozitia si comandam inceperea calculului folosind adancimea selectata
    snprintf(cmd, sizeof(cmd), "position fen %s\ngo depth %d\n", fen, botDepth);
    write(sf_write_fd, cmd, strlen(cmd));

    sf_buf_len = 0;
    memset(sf_bestmove, 0, sizeof(sf_bestmove));
}

// verifica daca motorul a returnat o mutare ('bestmove') in urma comenzii 'go'
// returneaza 1 daca mutarea a fost primita si parseaza rezultatul in sf_bestmove
static int sf_poll_move(void)
{
    if (sf_read_fd < 0) return 0;

    int n = (int)read(sf_read_fd, sf_buf + sf_buf_len,
                      (int)sizeof(sf_buf) - sf_buf_len - 1);
    if (n > 0) sf_buf_len += n;
    sf_buf[sf_buf_len] = '\0';

    char *bm = strstr(sf_buf, "bestmove ");
    if (bm) {
        sscanf(bm, "bestmove %7s", sf_bestmove);
        sf_buf_len = 0;
        return 1; // am gasit mutarea ideala calculata de bot
    }
    return 0; // inca se calculeaza
}

// interpreteaza si aplica pe tabla mutarea returnata de Stockfish (aflata in sf_bestmove)
static void execute_sf_move(void)
{
    int c1 = sf_bestmove[0] - 'a';
    int r1 = 8 - (sf_bestmove[1] - '0');
    int c2 = sf_bestmove[2] - 'a';
    int r2 = 8 - (sf_bestmove[3] - '0');

    char promo = 0;
    if (sf_bestmove[4] != '\0' && sf_bestmove[4] != ' ') {
        // stockfish trimite litera mica (ex: "e7e8q")
        promo = (current_turn == 0)
            ? (char)toupper((unsigned char)sf_bestmove[4])
            : sf_bestmove[4];
    }

    execute_move(r1, c1, r2, c2, promo ? promo : 'Q');
    current_turn = 1 - current_turn;
}

/* ───────────── /bot ───────────── */

//simboluri de sah in unicode
static const char *piece_sym(char p)
{
    switch (p) {
        case 'K': return "\xe2\x99\x94";   /* ♔ */
        case 'Q': return "\xe2\x99\x95";   /* ♕ */
        case 'R': return "\xe2\x99\x96";   /* ♖ */
        case 'B': return "\xe2\x99\x97";   /* ♗ */
        case 'N': return "\xe2\x99\x98";   /* ♘ */
        case 'P': return "\xe2\x99\x99";   /* ♙ */
        case 'k': return "\xe2\x99\x9a";   /* ♚ */
        case 'q': return "\xe2\x99\x9b";   /* ♛ */
        case 'r': return "\xe2\x99\x9c";   /* ♜ */
        case 'b': return "\xe2\x99\x9d";   /* ♝ */
        case 'n': return "\xe2\x99\x9e";   /* ♞ */
        case 'p': return "\xe2\x99\x9f";   /* ♟ */
        default:  return "";
    }
}

//fallback daca nu sunt acceptate simbolurile
static char gFallbackBuf[3];
static const char *piece_display(char p)
{
    if (gFontHasChess) return piece_sym(p);
    gFallbackBuf[0] = (char)toupper((unsigned char)p);
    gFallbackBuf[1] = '\0';
    return gFallbackBuf;
}

// functii ajutatoare pentru interfata grafica (GUI)

// transforma coordonatele in pixeli ale mouse-ului in coordonate (linie, coloana) pe tabla
// returneaza false daca click-ul a fost in afara tablei
static bool PixToBoard(Vector2 mp, int *r, int *c)
{
    int bx = (int)mp.x - BOARD_X;
    int by = (int)mp.y - BOARD_Y;
    if (bx < 0 || by < 0 || bx >= BOARD_PX || by >= BOARD_PX) return false;
    *c = bx / SQ;
    *r = by / SQ;
    return true;
}

// calculeaza si memoreaza in matricea 'legal' toate mutarile posibile pentru piesa de la (r, c)
static void ComputeLegal(int r, int c)
{
    memset(legal, 0, sizeof(legal));
    for (int dr = 0; dr < 8; dr++)
        for (int dc = 0; dc < 8; dc++)
            if (pseudo_legal(r, c, dr, dc, current_turn) &&
                try_move_legal(r, c, dr, dc, current_turn))
                legal[dr][dc] = 1;
}

//deseneaza un buton rotunjit care returneaza true daca este apasat
static bool Btn(Rectangle r, const char *txt, bool dis)
{
    Vector2 mouse = GetMousePosition();
    bool hov = !dis && CheckCollisionPointRec(mouse, r);
    Color bg = dis ? C_BTN_DIS : (hov ? C_BTN_HOV : C_BTN);
    Color border = dis ? DARKGRAY  : (hov ? LIME : GREEN);

    DrawRectangleRounded(r, 0.25f, 8, bg);
    DrawRectangleRoundedLines(r, 0.25f, 8, border);

    Vector2 ts = MeasureTextEx(gFont, txt, 20, 1);
    DrawTextEx(gFont, txt,
               (Vector2){ r.x + (r.width  - ts.x) * 0.5f,
                          r.y + (r.height - ts.y) * 0.5f },
               20, 1, dis ? GRAY : WHITE);

    return hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

// deseneaza o piesa centrata la (cx, cy) cu raza data
static void DrawPieceAt(char p, float cx, float cy, float rad)
{
    if (p == '.') return;

    //umbra la piesa
    DrawCircle((int)(cx + 2), (int)(cy + 3), rad, (Color){0, 0, 0, 80});

    Color border = is_white(p) ? (Color){190, 170, 130, 255} : (Color){ 70,  70,  70, 255};
    Color fill = is_white(p) ? C_WHITE_P : C_BLACK_P;
    Color ink = is_white(p) ? (Color){ 30,  30,  30, 255} : (Color){220, 220, 200, 255};

    DrawCircle((int)cx, (int)cy, rad + 2.0f, border);
    DrawCircle((int)cx, (int)cy, rad, fill);

    const char *sym = piece_display(p);
    float fs = rad * 1.45f;
    Vector2 tsz = MeasureTextEx(gFont, sym, fs, 0);
    DrawTextEx(gFont, sym, (Vector2){ cx - tsz.x * 0.5f, cy - tsz.y * 0.5f - 1.0f }, fs, 0, ink);
}

// deseneaza o piesa intr-un patrat al tablei
static void DrawPiece(char p, float x, float y)
{
    DrawPieceAt(p, x + SQ * 0.5f, y + SQ * 0.5f, SQ * 0.38f);
}

//ecranul de start
void DrawHome(void)
{
    ClearBackground(C_BG);

    //model de tabla blurat in spate
    int tw = WIN_W / 12, th = WIN_H / 10;
    for (int rr = 0; rr < 10; rr++)
        for (int cc = 0; cc < 12; cc++) {
            unsigned char v = (rr + cc) % 2 == 0 ? 30 : 24;
            DrawRectangle(cc * tw, rr * th, tw + 1, th + 1, (Color){v, v, v, 255});
        }

    // titlu
    const char *title = "CHESS GAME";
    int titleSize = 80;
    Vector2 tv = MeasureTextEx(gFont, title, titleSize, 2);
    DrawTextEx(gFont, title, (Vector2){ (WIN_W - tv.x) * 0.5f, 65.0f }, titleSize, 2, WHITE);

    //subtitlu
    const char *sub = "Select a game mode";
    Vector2 sv = MeasureTextEx(gFont, sub, 20, 1);
    DrawTextEx(gFont, sub, (Vector2){ (WIN_W - sv.x) * 0.5f, 175.0f }, 20, 1, LIGHTGRAY);

    float bw = 380.0f, bh = 58.0f, bx = (WIN_W - bw) * 0.5f;

    // buton 1v1 local
    Rectangle b1 = { bx, 230, bw, bh };
    if (Btn(b1, "Local 1v1", false)) {
        botMode = 0;
        init_board();
        current_turn = 0;
        selRow = selCol = -1;
        hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
        gameSt = ST_SELECT;
        curScreen = SCR_GAME;
    }

    //contra bot - acum activat!
    Rectangle b3 = { bx, 300, bw, bh };
    if (Btn(b3, "Play Against Bot", false)) {
        curScreen = SCR_BOTSETUP;
    }
}

//ecranul de selectare a dificultatii botului
void DrawBotSetup(void)
{
    ClearBackground(C_BG);

    //model de tabla blurat in spate
    int tw = WIN_W / 12, th = WIN_H / 10;
    for (int rr = 0; rr < 10; rr++)
        for (int cc = 0; cc < 12; cc++) {
            unsigned char v = (rr + cc) % 2 == 0 ? 30 : 24;
            DrawRectangle(cc * tw, rr * th, tw + 1, th + 1, (Color){v, v, v, 255});
        }

    // titlu
    const char *title = "PLAY AGAINST BOT";
    int titleSize = 60;
    Vector2 tv = MeasureTextEx(gFont, title, titleSize, 2);
    DrawTextEx(gFont, title, (Vector2){ (WIN_W - tv.x) * 0.5f, 65.0f }, titleSize, 2, WHITE);

    //subtitlu
    const char *sub = "Choose difficulty (you play as White)";
    Vector2 sv = MeasureTextEx(gFont, sub, 20, 1);
    DrawTextEx(gFont, sub, (Vector2){ (WIN_W - sv.x) * 0.5f, 155.0f }, 20, 1, LIGHTGRAY);

    load_stats();
    char statBuf[128];
    snprintf(statBuf, sizeof(statBuf), "Stats:  Wins: %d  |  Losses: %d  |  Streak: %d", pWins, pLosses, pStreak);
    Vector2 stv = MeasureTextEx(gFont, statBuf, 18, 1);
    DrawTextEx(gFont, statBuf, (Vector2){ (WIN_W - stv.x) * 0.5f, 190.0f }, 18, 1, GOLD);

    float bw = 380.0f, bh = 58.0f, bx = (WIN_W - bw) * 0.5f;

    // verificam daca exista stockfish
    bool sfExists = FileExists(SF_PATH);

    if (!sfExists) {
        const char *err = "Stockfish engine not found!";
        Vector2 ev = MeasureTextEx(gFont, err, 22, 1);
        DrawTextEx(gFont, err, (Vector2){ (WIN_W - ev.x) * 0.5f, 215.0f }, 22, 1, RED);

        const char *hint = "Expected at: stockfish/src/stockfish";
        Vector2 hv = MeasureTextEx(gFont, hint, 16, 1);
        DrawTextEx(gFont, hint, (Vector2){ (WIN_W - hv.x) * 0.5f, 245.0f }, 16, 1, GRAY);
    }

    // Easy
    Rectangle b1 = { bx, 290, bw, bh };
    if (Btn(b1, "Easy", !sfExists)) {
        botDepth = 1;
        botMode = 1;
        if (sf_start()) {
            init_board();
            current_turn = 0;
            selRow = selCol = -1;
            hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
            gameSt = ST_SELECT;
            curScreen = SCR_GAME;
        }
    }

    // Medium
    Rectangle b2 = { bx, 360, bw, bh };
    if (Btn(b2, "Medium", !sfExists)) {
        botDepth = 5;
        botMode = 1;
        if (sf_start()) {
            init_board();
            current_turn = 0;
            selRow = selCol = -1;
            hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
            gameSt = ST_SELECT;
            curScreen = SCR_GAME;
        }
    }

    // Hard
    Rectangle b3 = { bx, 430, bw, bh };
    if (Btn(b3, "Hard", !sfExists)) {
        botDepth = 12;
        botMode = 1;
        if (sf_start()) {
            init_board();
            current_turn = 0;
            selRow = selCol = -1;
            hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
            gameSt = ST_SELECT;
            curScreen = SCR_GAME;
        }
    }

    // Back
    Rectangle b4 = { bx, 520, bw, bh };
    if (Btn(b4, "Back", false)) {
        curScreen = SCR_HOME;
    }
}

//ecran de joc
void DrawGame(void)
{
    ClearBackground(C_BG);
    Vector2 mouse = GetMousePosition();

    /* ── bot: polling pentru bestmove ── */
    if (gameSt == ST_BOT_THINKING && sf_poll_move()) {
        botMoveReadyTime = GetTime();
        gameSt = ST_BOT_READY;
    }
    if (gameSt == ST_BOT_READY && GetTime() - botMoveReadyTime >= 1.0) {
        execute_sf_move();
        selRow = selCol = -1;
        if (!has_legal_moves(current_turn)) {
            set_game_over();
        } else {
            gameSt = ST_SELECT;
        }
    }

    /* ── hint: polling pentru bestmove ── */
    if (gameSt == ST_HINT_THINKING && sf_poll_move()) {
        hintSrcCol = sf_bestmove[0] - 'a';
        hintSrcRow = 8 - (sf_bestmove[1] - '0');
        hintDstCol = sf_bestmove[2] - 'a';
        hintDstRow = 8 - (sf_bestmove[3] - '0');
        gameSt = ST_SELECT;
    }

    //calculeaza starea de sah
    bool inCheck = (gameSt != ST_GAMEOVER) && is_in_check(current_turn);

    //gaseste pozitia regelui pentru a evidentia ca e in sah
    int kingR = -1, kingC = -1;
    if (inCheck) get_king_pos(current_turn, &kingR, &kingC);

    //tabla

    //umbra tablei
    DrawRectangle(BOARD_X + 4, BOARD_Y + 4, BOARD_PX, BOARD_PX, (Color){0, 0, 0, 110});

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            float x = (float)(BOARD_X + c * SQ);
            float y = (float)(BOARD_Y + r * SQ);

            //patrat de baza
            Color sq = ((r + c) % 2 == 0) ? C_LIGHT : C_DARK;
            DrawRectangle((int)x, (int)y, SQ, SQ, sq);

            //evidentierea piesei selectate
            if (r == selRow && c == selCol)
                DrawRectangle((int)x, (int)y, SQ, SQ, C_SEL);

            //evidentierea regelui in sah
            if (r == kingR && c == kingC)
                DrawRectangle((int)x, (int)y, SQ, SQ, C_CHECK);

            // evidentiere hint
            if (r == hintSrcRow && c == hintSrcCol)
                DrawRectangle((int)x, (int)y, SQ, SQ, (Color){ 50, 150, 200, 150 });
            if (r == hintDstRow && c == hintDstCol)
                DrawRectangle((int)x, (int)y, SQ, SQ, (Color){ 50, 150, 200, 200 });

            //indici pentru miscari legale
            if (selRow >= 0 && legal[r][c]) {
                float cx2 = x + SQ * 0.5f, cy2 = y + SQ * 0.5f;
                if (board[r][c] == '.')
                    DrawCircle((int)cx2, (int)cy2, SQ * 0.15f, C_HINT);
                else
                    DrawRing((Vector2){cx2, cy2}, SQ * 0.35f, SQ * 0.44f, 0, 360, 32, C_HINT);
            }
        }
    }

    //bordura tablei
    DrawRectangleLinesEx((Rectangle){ BOARD_X, BOARD_Y, BOARD_PX, BOARD_PX }, 2, DARKGRAY);

    //etichete randuri
    for (int r = 0; r < 8; r++) {
        char lbl[2] = { (char)('0' + (8 - r)), '\0' };
        DrawTextEx(gFont, lbl, (Vector2){ BOARD_X - 22.0f, BOARD_Y + r * SQ + SQ * 0.5f - 9.0f }, 18, 0, LIGHTGRAY);
    }
    //etichete coloane
    for (int c = 0; c < 8; c++) {
        char lbl[2] = { (char)('a' + c), '\0' };
        DrawTextEx(gFont, lbl, (Vector2){ BOARD_X + c * SQ + SQ * 0.5f - 6.0f, BOARD_Y + BOARD_PX + 8.0f }, 18, 0, LIGHTGRAY);
    }

    //piese
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            DrawPiece(board[r][c], (float)(BOARD_X + c * SQ), (float)(BOARD_Y + r * SQ));

    //panou din dreapta
    DrawRectangle(PANEL_X - 10, PANEL_Y, PANEL_W + 10, PANEL_H, C_PANEL);
    DrawRectangleLinesEx((Rectangle){ PANEL_X - 10, PANEL_Y, PANEL_W + 10, PANEL_H }, 1, DARKGRAY);

    float px = (float)(PANEL_X + 5);
    float py = (float)(PANEL_Y + 18);

    //eticheta "turn"
    const char *turnLbl = "TURN";
    Vector2 tlv = MeasureTextEx(gFont, turnLbl, 14, 2);
    DrawTextEx(gFont, turnLbl, (Vector2){ px + (PANEL_W - tlv.x) * 0.5f, py }, 14, 2, GRAY);
    py += 26.0f;

    //mostra de culoare + numele jucatorului
    const char *who;
    if (botMode)
        who = (current_turn == 0) ? "You (White)" : "Bot (Black)";
    else
        who = (current_turn == 0) ? "White" : "Black";
    Color swFill = (current_turn == 0) ? WHITE : (Color){ 30, 30, 30, 255};

    DrawCircle((int)(px + 20), (int)(py + 16), 17, LIGHTGRAY);
    DrawCircle((int)(px + 20), (int)(py + 16), 14, swFill);

    DrawTextEx(gFont, who, (Vector2){ px + 44, py + 4 }, 26, 1, WHITE);
    py += 58.0f;

    //separator
    DrawLine((int)px, (int)py, (int)(px + PANEL_W - 20), (int)py, DARKGRAY);
    py += 14.0f;

    //advertisment de sah
    if (inCheck) {
        const char *chkTxt = "CHECK!";
        Vector2 cv = MeasureTextEx(gFont, chkTxt, 26, 1);
        DrawTextEx(gFont, chkTxt, (Vector2){ px + (PANEL_W - cv.x) * 0.5f - 5.0f, py }, 26, 1, RED);
        py += 36.0f;
    }

    // indicator de incarcare afisat cat timp botul se gandeste sau asteptam un hint
    if (gameSt == ST_BOT_THINKING || gameSt == ST_BOT_READY || gameSt == ST_HINT_THINKING) {
        // animatie cu puncte
        int dots = ((int)(GetTime() * 3.0)) % 4;
        char thinkTxt[20];
        snprintf(thinkTxt, sizeof(thinkTxt), "Thinking%.*s", dots, "...");
        Vector2 tv2 = MeasureTextEx(gFont, thinkTxt, 22, 1);
        DrawTextEx(gFont, thinkTxt, (Vector2){ px + (PANEL_W - tv2.x) * 0.5f - 5.0f, py }, 22, 1, GOLD);
        py += 32.0f;
    }

    //info dificultate in bot mode
    if (botMode && gameSt != ST_GAMEOVER) {
        const char *diffTxt = (botDepth <= 1) ? "Easy" : (botDepth <= 5) ? "Medium" : "Hard";
        char diffLabel[32];
        snprintf(diffLabel, sizeof(diffLabel), "Difficulty: %s", diffTxt);
        Vector2 dv = MeasureTextEx(gFont, diffLabel, 16, 1);
        DrawTextEx(gFont, diffLabel, (Vector2){ px + (PANEL_W - dv.x) * 0.5f - 5.0f, py }, 16, 1, LIGHTGRAY);
    }

    // butoanele panoului
    float btnY = (float)(PANEL_Y + PANEL_H - 170);
    Rectangle rHint = { (float)PANEL_X, btnY, (float)(PANEL_W - 20), 45 };
    Rectangle rNew = { (float)PANEL_X, btnY + 57.0f, (float)(PANEL_W - 20), 45 };
    Rectangle rMenu = { (float)PANEL_X, btnY + 114.0f,(float)(PANEL_W - 20), 45 };

    if (Btn(rHint, "Suggest Move", gameSt != ST_SELECT)) {
        if (sf_pid <= 0) sf_start();
        if (sf_pid > 0) {
            gameSt = ST_HINT_THINKING;
            sf_request_move();
        }
    }
    if (Btn(rNew,  "New Game",  false)) {
        init_board();
        current_turn = 0;
        selRow = selCol = -1;
        hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
        gameSt = ST_SELECT;
    }
    if (Btn(rMenu, "Main Menu", false)) {
        curScreen = SCR_HOME;
        selRow = selCol = -1;
        hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
        gameSt = ST_SELECT;
    }

    //overlay promovare pion
    if (gameSt == ST_PROMOTE) {
        DrawRectangle(0, 0, WIN_W, WIN_H, (Color){0, 0, 0, 160});

        float dw = 360, dh = 200;
        float dx = (WIN_W - dw) * 0.5f, dy = (WIN_H - dh) * 0.5f;

        DrawRectangleRounded((Rectangle){ dx, dy, dw, dh }, 0.15f, 8, (Color){ 50, 50, 50, 255 });
        DrawRectangleRoundedLines((Rectangle){ dx, dy, dw, dh }, 0.15f, 8, WHITE);

        const char *ptitle = "Promote pawn to:";
        Vector2 ptv = MeasureTextEx(gFont, ptitle, 20, 1);
        DrawTextEx(gFont, ptitle, (Vector2){ dx + (dw - ptv.x) * 0.5f, dy + 12 }, 20, 1, WHITE);

        static const char *pKeys[] = { "Q", "R", "B", "N" };
        static const char *pName[] = { "Queen", "Rook", "Bishop", "Knight" };
        float bw2 = 72, bh2 = 95, gap = 12;
        float sx = dx + (dw - 4 * (bw2 + gap) + gap) * 0.5f;

        for (int i = 0; i < 4; i++) {
            Rectangle pb = { sx + i * (bw2 + gap), dy + 52, bw2, bh2 };
            bool hov = CheckCollisionPointRec(mouse, pb);

            DrawRectangleRounded(pb, 0.2f, 6, hov ? C_BTN_HOV : C_BTN);
            DrawRectangleRoundedLines(pb, 0.2f, 6, hov ? LIME : GREEN);

            //previzualizare piesa
            char pp = (current_turn == 0) ? pKeys[i][0] : (char)tolower((unsigned char)pKeys[i][0]);
            DrawPieceAt(pp, pb.x + bw2 * 0.5f, pb.y + bh2 * 0.40f, bh2 * 0.28f);

            Vector2 nv = MeasureTextEx(gFont, pName[i], 11, 0);
            DrawTextEx(gFont, pName[i], (Vector2){ pb.x + (bw2 - nv.x) * 0.5f, pb.y + bh2 - 18 }, 11, 0, LIGHTGRAY);

            if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                char promChar = (current_turn == 0) ? pKeys[i][0] : (char)tolower((unsigned char)pKeys[i][0]);
                execute_move(promSrcRow, promSrcCol, promDstRow, promDstCol, promChar);
                current_turn = 1 - current_turn;
                selRow = selCol = -1;

                if (!has_legal_moves(current_turn)) {
                    set_game_over();
                } else if (botMode && current_turn == 1) {
                    // dupa promovarea jucatorului, botul muta
                    gameSt = ST_BOT_THINKING;
                    sf_request_move();
                } else {
                    gameSt = ST_SELECT;
                }
            }
        }
    }

    //overlay sfarsit de joc
    if (gameSt == ST_GAMEOVER) {
        DrawRectangle(0, 0, WIN_W, WIN_H, (Color){0, 0, 0, 160});

        float dw = 420, dh = 230;
        float dx = (WIN_W - dw) * 0.5f, dy = (WIN_H - dh) * 0.5f;

        DrawRectangleRounded((Rectangle){ dx, dy, dw, dh }, 0.15f, 8, (Color){ 48, 48, 48, 255 });
        DrawRectangleRoundedLines((Rectangle){ dx, dy, dw, dh }, 0.15f, 8, WHITE);

        bool mate = is_in_check(current_turn);
        const char *hdr = mate ? "CHECKMATE" : "STALEMATE";
        const char *sub2;
        if (mate) {
            if (botMode)
                sub2 = (current_turn == 0) ? "Bot wins!" : "You win!";
            else
                sub2 = (current_turn == 0) ? "Black wins!" : "White wins!";
        } else {
            sub2 = "Draw \xe2\x80\x94 no legal moves";
        }

        Vector2 hv = MeasureTextEx(gFont, hdr, 52, 2);
        DrawTextEx(gFont, hdr, (Vector2){ dx + (dw - hv.x) * 0.5f, dy + 22 }, 52, 2, WHITE);

        Vector2 sv2 = MeasureTextEx(gFont, sub2, 26, 1);
        DrawTextEx(gFont, sub2, (Vector2){ dx + (dw - sv2.x) * 0.5f, dy + 92 }, 26, 1, GOLD);

        Rectangle btnPA = { dx + (dw - 200) * 0.5f, dy + dh - 65, 200, 44 };
        if (Btn(btnPA, "Play Again", false)) {
            init_board();
            current_turn = 0;
            selRow = selCol = -1;
            hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
            gameSt = ST_SELECT;
        }
    }

    // proceseaza inputul jucatorului pe tabla (permis doar in starea ST_SELECT)
    if (gameSt == ST_SELECT && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1; // sterge vizualizarea hint-ului la orice click

        // previne mutarile facute de om atunci cand este randul botului (negru)
        if (botMode && current_turn == 1) {
            // masura de precautie: click-urile sunt ignorate in acest moment
        } else {
            int cr, cc; // rand curent (current row) si coloana curenta (current column) deduse din click
            if (PixToBoard(mouse, &cr, &cc)) {
                if (selRow < 0) {
                    //primul click selecteaza o piesa proprie
                    if (board[cr][cc] != '.' && is_own(board[cr][cc], current_turn)) {
                        selRow = cr; selCol = cc;
                        ComputeLegal(cr, cc);
                    }
                } else if (cr == selRow && cc == selCol) {
                    //click pe piesa selectata -> deselecteaza
                    selRow = selCol = -1;
                } else if (board[cr][cc] != '.' && is_own(board[cr][cc], current_turn)) {
                    // schimba la alta piesa proprie
                    selRow = cr; selCol = cc;
                    ComputeLegal(cr, cc);
                } else if (legal[cr][cc]) {
                    //Destinatie valida -> incearca mutarea
                    char moved = board[selRow][selCol];
                    bool isProm = (toupper((unsigned char)moved) == 'P') && ((current_turn == 0 && cr == 0) || (current_turn == 1 && cr == 7));

                    if (isProm) {
                        // am declansat o promovare, salvam datele si aratam meniul de promovare
                        promSrcRow = selRow; promSrcCol = selCol;
                        promDstRow = cr;     promDstCol = cc;
                        selRow = selCol = -1;   // sterge vizual patratul selectat pentru a nu se suprapune cu meniul
                        gameSt = ST_PROMOTE;
                    } else {
                        // mutare normala
                        execute_move(selRow, selCol, cr, cc, 'Q');  // 'Q' e transmis ca placeholder ignorat
                        current_turn = 1 - current_turn; // schimba randul jucatorului
                        selRow = selCol = -1; // deselecteaza

                        // verifica daca dupa aceasta mutare jocul s-a terminat
                        if (!has_legal_moves(current_turn)) {
                            set_game_over();
                        } else if (botMode && current_turn == 1) {
                            // daca jocul continua si e modul vs bot, incepem sa cerem mutarea botului
                            gameSt = ST_BOT_THINKING;
                            sf_request_move();
                        }
                    }
                } else {
                    // s-a dat click pe un patrat valid dar care nu corespunde niciunei mutari posibile
                    selRow = selCol = -1;
                }
            }
        }
    }
}

static bool gFontLoaded = false;

void init_fonts(void)
{
    //incarcarea fontului
    /* incearca sa incarce un font cu simboluri unicode pentru sah (U+2654..U+265F).
       foloseste fontul implicit din raylib daca nu este gasit niciunul.    */
    static const char *kFontPaths[] = {
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",  /* macOS  */
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Geneva.ttf",                       /* macOS fallback */
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",        /* Linux  */
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "C:\\Windows\\Fonts\\seguisym.ttf",                       /* Win    */
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    int numPaths = (int)(sizeof(kFontPaths) / sizeof(kFontPaths[0]));

    // construieste lista de coduri: ASCII vizibil + simboluri de sah
    int cpCount = 95 + 12;
    int *cps = (int *)malloc(cpCount * sizeof(int));
    for (int i = 0; i < 95; i++) cps[i] = 32 + i;
    for (int i = 0; i < 12; i++) cps[95 + i] = 0x2654 + i;  /* ♔ to ♟ */

    gFontLoaded = false;
    for (int i = 0; i < numPaths && !gFontLoaded; i++) {
        if (FileExists(kFontPaths[i])) {
            gFont = LoadFontEx(kFontPaths[i], 64, cps, cpCount);
            if (gFont.glyphCount > 0) gFontLoaded = true;
        }
    }
    free(cps);

    if (gFontLoaded) {
        // verifica daca fontul incarcat contine simboluri de sah
        int idx = GetGlyphIndex(gFont, 0x2654);
        gFontHasChess = (idx > 0);
        SetTextureFilter(gFont.texture, TEXTURE_FILTER_BILINEAR);
    } else {
        gFont = GetFontDefault();
        TraceLog(LOG_WARNING,
                 "GUI: Unicode font not found - pieces shown as letters");
    }
}

void cleanup_fonts(void)
{
    if (gFontLoaded) UnloadFont(gFont);
}

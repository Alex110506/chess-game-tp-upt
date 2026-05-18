/*
 * chess_gui.c  —  Raylib GUI front-end for chess_logic.c
 *
 * Build:  make gui
 * Run:    ./chess_gui
 */

#include "raylib.h"
#include "chess_logic.h"
#include "chess_gui.h"
#include "chess_net.h"
#include "chess_auth.h"
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
typedef enum {
    ST_SELECT,
    ST_PROMOTE,
    ST_GAMEOVER,
    ST_BOT_THINKING,
    ST_BOT_READY,
    ST_HINT_THINKING,
    ST_WAIT_OPP        // multiplayer: asteptam mutarea oponentului
} GameSt;

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

// stare ceas de joc: tic per cadru cand este randul jucatorului in curs
static bool   gTimerEnabled = false;   // adevarat daca jocul are timer
static int    gTimeInitial  = 0;       // secundele initiale
static double gWhiteTime    = 0.0;     // timpul ramas pt alb
static double gBlackTime    = 0.0;     // timpul ramas pt negru
static double gLastTickT    = 0.0;     // GetTime() la ultimul cadru
static int    gFlagged      = -1;      // -1 niciunul, 0 alb a ramas fara timp, 1 negrul

// dupa selectia timpului, unde mergem?
typedef enum {
    TIME_NEXT_LOCAL_1V1,
    TIME_NEXT_MP_HOST,
} TimeNextAction;
static TimeNextAction gTimeNext = TIME_NEXT_LOCAL_1V1;

// stare ecran login: bufferele de input + focus + mesaje
static char loginUser[AUTH_USERNAME_MAX] = "";
static char loginPass[64] = "";
static int  loginFocus = 0;            // 0 = username, 1 = parola
static char loginStatus[128] = "";     // mesaj de eroare/info
static Color loginStatusColor = { 220, 60, 60, 255 };
static bool loginBusy = false;         // true cat timp ruleaza un request HTTP

// stare ecran multiplayer: numele oponentului si rezultat (pentru report ELO)
static char mpOpponent[AUTH_USERNAME_MAX] = "";
static bool mpResultReported = false;

// gate pentru multiplayer cand nu suntem logati
static bool showLoginGate = false;

// stare mod "puzzle" (probleme de sah cu sah-mat in N mutari)
static int  puzzleMode = 0;        // 1 = jucam un puzzle
static int  puzzleIdx = 0;         // indexul puzzle-ului curent
static int  puzzleUserColor = 0;   // culoarea cu care joaca jucatorul in puzzle (0 alb / 1 negru)
static int  puzzleTarget = 0;      // numarul de mutari maxim pentru sah-mat
static int  puzzleMoveCount = 0;   // mutarile facute pana acum de jucator
static bool puzzleSolved = false;  // true daca jucatorul a livrat sah-mat
static bool puzzleFailed = false;  // true daca jucatorul nu a reusit sah-mat in N mutari

// definitia unui puzzle: pozitie + cine muta + numarul tinta de mutari
// difficulty: 1 = easy, 2 = medium, 3 = hard
typedef struct {
    const char *name;
    const char *desc;
    char setup[8][9];   // 8 randuri x 8 caractere + null
    int side_to_move;   // 0 alb / 1 negru
    int target;         // sah-mat in N mutari
    int difficulty;     // 1 easy / 2 medium / 3 hard
} Puzzle;

// colectie de puzzle-uri predefinite verificate manual
static const Puzzle gPuzzles[] = {
    {
        "Back Rank Mate",
        "White to move. Deliver mate in 1.",
        {
            "......k.",
            ".....ppp",
            "........",
            "........",
            "........",
            "........",
            "........",
            "R.....K."
        },
        0, 1, 1
    },
    {
        "Queen's Crown",
        "White to move. Mate in 1 with king support.",
        {
            ".......k",
            "........",
            ".....K..",
            "........",
            "........",
            "......Q.",
            "........",
            "........"
        },
        0, 1, 1
    },
    {
        "Arabian Mate",
        "White to move. Mate in 1 with rook and knight.",
        {
            ".......k",
            "........",
            ".....N.K",
            "........",
            "........",
            "........",
            "........",
            "R......."
        },
        0, 1, 1
    },
    {
        "Promotion Push",
        "White to move. Crown a pawn to deliver mate.",
        {
            "..k.....",
            "P.......",
            "..K.....",
            "........",
            "........",
            "........",
            "........",
            "........"
        },
        0, 1, 1
    },
    {
        "Smothered Mate",
        "White to move. The knight finishes the job in 1.",
        {
            "......rk",
            "......pp",
            ".......N",
            "........",
            "........",
            "........",
            "........",
            "......K."
        },
        0, 1, 2
    },
    {
        "Anastasia's Mate",
        "White to move. A rook lift seals the corner. Mate in 1.",
        {
            "........",
            "....N.pk",
            "........",
            "........",
            "........",
            "....K...",
            "........",
            "R......."
        },
        0, 1, 2
    },
    {
        "Trapped King",
        "White to move. Take what defends the corner. Mate in 1.",
        {
            "........",
            "........",
            "........",
            "........",
            ".......Q",
            "......K.",
            ".......p",
            ".......k"
        },
        0, 1, 2
    },
    {
        "Double Check",
        "White to move. A discovered double check is unstoppable.",
        {
            "Q.N...k.",
            "......p.",
            "......K.",
            "........",
            "........",
            "........",
            "........",
            "........"
        },
        0, 1, 3
    },
    {
        "Pawn Crusher",
        "White to move. Corner the king with two rooks. Mate in 2.",
        {
            "k.......",
            "pp......",
            "..K.....",
            "........",
            "........",
            "........",
            "........",
            ".R....R."
        },
        0, 2, 3
    }
};

static const int gPuzzleCount = (int)(sizeof(gPuzzles) / sizeof(gPuzzles[0]));

// stare multiplayer
static int  mpMode = 0;            // 1 = joc online prin server
static int  mpMyColor = 0;         // 0 = alb, 1 = negru (coloarea pe care o jucam)
static char mpRoomCode[8] = "";    // codul camerei curente (afisat pe ecran)
static char mpJoinInput[8] = "";   // bufferul de input pentru codul de join
static char mpStatus[160] = "";    // mesaj informativ afisat in UI (erori, info)
static bool mpHosting = false;     // 1 daca tocmai am cerut create si asteptam codul
static bool mpOpponentLeft = false;

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

// forward decl pentru raportarea rezultatului online
static void mp_report_result(const char *result);

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
    } else if (puzzleMode) {
        // in modul puzzle: succes daca oponentul e in sah-mat
        bool mate = is_in_check(current_turn);
        puzzleSolved = mate && (current_turn != puzzleUserColor);
        puzzleFailed = !puzzleSolved;
    } else if (mpMode) {
        // raporteaza rezultatul partidei online catre backend (ELO)
        bool mate = is_in_check(current_turn);
        if (mate) {
            // jucatorul care sta sa mute e mat -> pierde
            const char *r = (current_turn == mpMyColor) ? "loss" : "win";
            mp_report_result(r);
        } else {
            mp_report_result("draw");
        }
    }
}

// declaratii forward pentru functiile Stockfish (definite mai jos)
static int sf_start(void);
static void sf_request_move(void);

// ─── helper-i multiplayer ────────────────────────────────────────────────

// transforma coordonatele tablei + caracter de promovare in notatie UCI
// (ex: r1=6,c1=4,r2=4,c2=4 -> "e2e4"; cu promo='Q' -> "e7e8q")
static void coords_to_uci(int r1, int c1, int r2, int c2, char promo, char out[8])
{
    out[0] = (char)('a' + c1);
    out[1] = (char)('0' + (8 - r1));
    out[2] = (char)('a' + c2);
    out[3] = (char)('0' + (8 - r2));
    if (promo) {
        out[4] = (char)tolower((unsigned char)promo);
        out[5] = '\0';
    } else {
        out[4] = '\0';
    }
}

// reseteaza starea de multiplayer si tabla
static void mp_reset_state(void)
{
    mpMode = 0;
    mpMyColor = 0;
    mpRoomCode[0] = '\0';
    mpJoinInput[0] = '\0';
    mpStatus[0] = '\0';
    mpHosting = false;
    mpOpponentLeft = false;
}

// inchide bridge-ul de retea (apelat la iesire si la parasirea jocului online)
void mp_cleanup(void)
{
    if (net_running()) {
        net_stop();
    }
    mp_reset_state();
}

// aplica pe tabla o mutare primita prin retea, in format UCI
// returneaza 1 daca a fost aplicata cu succes
static int mp_apply_remote_move(const char *uci)
{
    if (!uci || strlen(uci) < 4) return 0;
    int c1 = uci[0] - 'a';
    int r1 = 8 - (uci[1] - '0');
    int c2 = uci[2] - 'a';
    int r2 = 8 - (uci[3] - '0');
    if (c1 < 0 || c1 > 7 || r1 < 0 || r1 > 7 ||
        c2 < 0 || c2 > 7 || r2 < 0 || r2 > 7) return 0;

    // verifica ca mutarea respecta regulile (apararea de baza)
    if (!pseudo_legal(r1, c1, r2, c2, current_turn)) return 0;
    if (!try_move_legal(r1, c1, r2, c2, current_turn)) return 0;

    char promo = 'Q';
    if (uci[4] != '\0') {
        promo = (current_turn == 0)
            ? (char)toupper((unsigned char)uci[4])
            : uci[4];
    }
    execute_move(r1, c1, r2, c2, promo);
    current_turn = 1 - current_turn;
    return 1;
}

// porneste un puzzle: incarca pozitia, deschide Stockfish si trece la SCR_GAME
static int start_puzzle(int idx)
{
    if (idx < 0 || idx >= gPuzzleCount) return 0;
    if (!FileExists(SF_PATH)) return 0;
    if (!sf_start()) return 0;

    puzzleMode = 1;
    puzzleIdx = idx;
    puzzleUserColor = gPuzzles[idx].side_to_move;
    puzzleTarget = gPuzzles[idx].target;
    puzzleMoveCount = 0;
    puzzleSolved = false;
    puzzleFailed = false;

    // dezactivam modurile concurente
    botMode = 0;
    mpMode = 0;
    gTimerEnabled = false;

    // adancimea pentru aparare in puzzle - destul de mare pentru a apara optim
    botDepth = 14;

    load_puzzle_position(gPuzzles[idx].setup, gPuzzles[idx].side_to_move);

    selRow = selCol = -1;
    hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
    gameSt = ST_SELECT;
    curScreen = SCR_GAME;
    return 1;
}

// porneste un joc online (din lobby cand vine 'start'). reseteaza tabla.
// 'time_seconds' este timpul pentru fiecare jucator (0 = fara timer).
static void mp_begin_game(int time_seconds)
{
    init_board();
    current_turn = 0;
    selRow = selCol = -1;
    hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
    botMode = 0;
    mpMode = 1;  // activeaza modul multiplayer
    mpOpponentLeft = false;
    mpResultReported = false;

    gTimerEnabled = (time_seconds > 0);
    gTimeInitial  = time_seconds;
    gWhiteTime    = (double)time_seconds;
    gBlackTime    = (double)time_seconds;
    gLastTickT    = GetTime();
    gFlagged      = -1;

    // gazda (alb) muta primul; daca eu sunt alb, sunt la rand
    gameSt = (mpMyColor == current_turn) ? ST_SELECT : ST_WAIT_OPP;
    curScreen = SCR_GAME;
}

// raporteaza rezultatul jocului multiplayer catre backend, daca utilizatorul
// este logat si suntem in mod multiplayer ranked. result: "win"|"loss"|"draw"
static void mp_report_result(const char *result)
{
    if (mpResultReported) return;
    if (!mpMode) return;
    if (!gAuth.logged_in) return;
    mpResultReported = true;
    char err[AUTH_ERR_MAX] = {0};
    if (auth_report_game(result, mpOpponent[0] ? mpOpponent : NULL, err, sizeof(err))) {
        // afisam noul rank in caseta de sfarsit
        snprintf(mpStatus, sizeof(mpStatus), "New rank: %d", gAuth.rank);
    } else {
        snprintf(mpStatus, sizeof(mpStatus), "Rank update failed: %.96s", err);
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

// returneaza true daca tabla trebuie afisata inversat (jucatorul cu negru)
static bool board_flipped(void)
{
    if (mpMode && mpMyColor == 1) return true;
    if (puzzleMode && puzzleUserColor == 1) return true;
    return false;
}

// transforma coordonatele in pixeli ale mouse-ului in coordonate (linie, coloana) pe tabla
// returneaza false daca click-ul a fost in afara tablei
// tine cont de inversarea tablei cand jucam cu piesele negre
static bool PixToBoard(Vector2 mp, int *r, int *c)
{
    int bx = (int)mp.x - BOARD_X;
    int by = (int)mp.y - BOARD_Y;
    if (bx < 0 || by < 0 || bx >= BOARD_PX || by >= BOARD_PX) return false;
    *c = bx / SQ;
    *r = by / SQ;
    if (board_flipped()) {
        *c = 7 - *c;
        *r = 7 - *r;
    }
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

// buton cu accent auriu (folosit pentru actiuni legate de abonament)
static bool BtnGold(Rectangle r, const char *txt, bool dis)
{
    Vector2 mouse = GetMousePosition();
    bool hov = !dis && CheckCollisionPointRec(mouse, r);
    Color bg     = dis ? C_BTN_DIS : (hov ? (Color){ 210, 165, 20, 255 } : (Color){ 170, 130, 10, 255 });
    Color border = dis ? DARKGRAY  : (hov ? WHITE : GOLD);

    DrawRectangleRounded(r, 0.25f, 8, bg);
    DrawRectangleRoundedLines(r, 0.25f, 8, border);

    Vector2 ts = MeasureTextEx(gFont, txt, 20, 1);
    DrawTextEx(gFont, txt,
               (Vector2){ r.x + (r.width  - ts.x) * 0.5f,
                          r.y + (r.height - ts.y) * 0.5f },
               20, 1, dis ? GRAY : (Color){ 20, 14, 0, 255 });

    return hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

// deschide un URL in browser-ul implicit al sistemului
static void open_in_browser(const char *url)
{
    char cmd[512];
#if defined(__APPLE__)
    snprintf(cmd, sizeof(cmd), "open \"%s\"", url);
#elif defined(_WIN32)
    snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", url);
#else
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", url);
#endif
    system(cmd);
}

// returneaza URL-ul frontend-ului web (configurabil prin CHESS_FRONTEND_URL)
static const char *frontend_url(void)
{
    static char g_frontend_url[256] = {0};
    if (!g_frontend_url[0]) {
        const char *env = getenv("CHESS_FRONTEND_URL");
        snprintf(g_frontend_url, sizeof(g_frontend_url), "%s",
                 (env && *env) ? env : "http://localhost:5173");
    }
    return g_frontend_url;
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

// deseneaza un buton mare cu o piesa de sah ca pictograma in stanga
// returneaza true daca a fost apasat
static bool BigBtn(Rectangle r, char piece, const char *title, const char *subtitle, bool dis)
{
    Vector2 mouse = GetMousePosition();
    bool hov = !dis && CheckCollisionPointRec(mouse, r);
    Color bg = dis ? C_BTN_DIS : (hov ? C_BTN_HOV : C_BTN);
    Color border = dis ? DARKGRAY : (hov ? LIME : GREEN);

    // umbra subtila sub buton
    DrawRectangleRounded((Rectangle){ r.x + 3, r.y + 4, r.width, r.height }, 0.22f, 8, (Color){0, 0, 0, 110});
    DrawRectangleRounded(r, 0.22f, 8, bg);
    DrawRectangleRoundedLines(r, 0.22f, 8, border);

    // pictograma piesa in stanga
    float iconCx = r.x + 38.0f;
    float iconCy = r.y + r.height * 0.5f;
    DrawPieceAt(piece, iconCx, iconCy, 22.0f);

    // titlul butonului
    float textX = r.x + 78.0f;
    Vector2 tsz = MeasureTextEx(gFont, title, 24, 1);
    DrawTextEx(gFont, title,
               (Vector2){ textX, r.y + (subtitle ? 12.0f : (r.height - tsz.y) * 0.5f) },
               24, 1, dis ? GRAY : WHITE);

    if (subtitle) {
        DrawTextEx(gFont, subtitle, (Vector2){ textX, r.y + 42.0f }, 14, 1,
                   dis ? (Color){90, 90, 90, 255} : (Color){200, 220, 200, 230});
    }

    return hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

//ecranul de start
void DrawHome(void)
{
    ClearBackground(C_BG);
    Vector2 mouse = GetMousePosition();

    // === fundal: model de tabla intunecata cu o vinieta usoara ===
    int tw = WIN_W / 12, th = WIN_H / 10;
    for (int rr = 0; rr < 10; rr++)
        for (int cc = 0; cc < 12; cc++) {
            unsigned char v = (rr + cc) % 2 == 0 ? 30 : 24;
            DrawRectangle(cc * tw, rr * th, tw + 1, th + 1, (Color){v, v, v, 255});
        }
    // overlay radial subtil pentru a focaliza atentia spre centru
    for (int i = 0; i < 4; i++) {
        DrawRectangle(0, 0, WIN_W, 60 + i * 12, (Color){0, 0, 0, 30});
        DrawRectangle(0, WIN_H - 60 - i * 12, WIN_W, 60 + i * 12, (Color){0, 0, 0, 30});
    }

    // === bara de profil sus-dreapta ===
    {
        float pbW = 230.0f, pbH = 44.0f;
        float pbX = WIN_W - pbW - 16.0f, pbY = 16.0f;
        Rectangle pb = { pbX, pbY, pbW, pbH };
        bool hov = CheckCollisionPointRec(mouse, pb);

        Color bg = hov ? (Color){55, 75, 60, 255} : (Color){40, 50, 42, 235};
        Color border = hov ? LIME : (Color){90, 130, 100, 255};
        DrawRectangleRounded(pb, 0.45f, 8, bg);
        DrawRectangleRoundedLines(pb, 0.45f, 8, border);

        // pictograma "user" rotunda
        DrawCircle((int)(pbX + 22), (int)(pbY + pbH * 0.5f), 14, (Color){25, 30, 26, 255});
        DrawCircle((int)(pbX + 22), (int)(pbY + pbH * 0.5f - 3), 5, (Color){200, 200, 200, 255});
        DrawRectangleRounded((Rectangle){ pbX + 14, pbY + pbH * 0.5f + 2, 16, 8 }, 0.5f, 8,
                             (Color){200, 200, 200, 255});

        if (gAuth.logged_in) {
            // username + rank
            DrawTextEx(gFont, gAuth.username, (Vector2){ pbX + 44, pbY + 6 }, 18, 1, WHITE);
            char rkbuf[24];
            snprintf(rkbuf, sizeof(rkbuf), "Rank %d", gAuth.rank);
            DrawTextEx(gFont, rkbuf, (Vector2){ pbX + 44, pbY + 24 }, 14, 1, GOLD);

            // badge PRO (sus-dreapta) daca abonamentul este activ
            bool hasPro = strcmp(gAuth.subscription, "pro")        == 0
                       || strcmp(gAuth.subscription, "cancelling") == 0;
            if (hasPro) {
                const char *proLbl = "PRO";
                Vector2 plv = MeasureTextEx(gFont, proLbl, 11, 1);
                float bpx = pbX + pbW - plv.x - 16.0f;
                float bpy = pbY + 5.0f;
                DrawRectangleRounded((Rectangle){ bpx - 5, bpy - 2, plv.x + 10, plv.y + 4 },
                                     0.6f, 8, GOLD);
                DrawTextEx(gFont, proLbl, (Vector2){ bpx, bpy }, 11, 1,
                           (Color){ 30, 20, 0, 255 });
            }
        } else {
            DrawTextEx(gFont, "Sign in", (Vector2){ pbX + 44, pbY + 6 }, 18, 1, WHITE);
            DrawTextEx(gFont, "to track rank", (Vector2){ pbX + 44, pbY + 24 }, 12, 1,
                       (Color){200, 220, 200, 230});
        }

        if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (gAuth.logged_in) {
                loginStatus[0] = '\0';
                curScreen = SCR_PROFILE;
            } else {
                loginStatus[0] = '\0';
                loginUser[0] = '\0';
                loginPass[0] = '\0';
                curScreen = SCR_LOGIN;
            }
        }
    }

    // === piese decorative langa titlu ===
    DrawPieceAt('K', 140.0f, 110.0f, 36.0f);
    DrawPieceAt('q', WIN_W - 140.0f, 110.0f, 36.0f);

    // === titlu cu umbra/glow ===
    const char *title = "CHESS GAME";
    int titleSize = 88;
    Vector2 tv = MeasureTextEx(gFont, title, titleSize, 3);
    float titleX = (WIN_W - tv.x) * 0.5f;
    float titleY = 70.0f;

    // straturi de umbra pentru efect de adancime
    for (int i = 3; i >= 1; i--) {
        DrawTextEx(gFont, title, (Vector2){ titleX + i, titleY + i }, titleSize, 3, (Color){0, 0, 0, 130});
    }
    DrawTextEx(gFont, title, (Vector2){ titleX, titleY }, titleSize, 3, (Color){245, 235, 205, 255});

    // accent auriu sub titlu
    float underlineW = tv.x * 0.45f;
    DrawRectangle((int)((WIN_W - underlineW) * 0.5f), (int)(titleY + tv.y + 8), (int)underlineW, 3, GOLD);

    // subtitlu
    const char *sub = "Choose your game mode";
    Vector2 sv = MeasureTextEx(gFont, sub, 22, 1);
    DrawTextEx(gFont, sub, (Vector2){ (WIN_W - sv.x) * 0.5f, titleY + tv.y + 22.0f }, 22, 1, LIGHTGRAY);

    // === grila 2x2 de butoane mari ===
    float bw = 340.0f, bh = 76.0f;
    float gap = 26.0f;
    float gridW = bw * 2 + gap;
    float bx0 = (WIN_W - gridW) * 0.5f;
    float byTop = 260.0f;

    Rectangle b1 = { bx0,              byTop,                  bw, bh };
    Rectangle b2 = { bx0 + bw + gap,   byTop,                  bw, bh };
    Rectangle b3 = { bx0,              byTop + bh + gap,       bw, bh };
    Rectangle b4 = { bx0 + bw + gap,   byTop + bh + gap,       bw, bh };

    if (BigBtn(b1, 'K', "Local 1v1", "Play side by side", false)) {
        gTimeNext = TIME_NEXT_LOCAL_1V1;
        curScreen = SCR_TIMESETUP;
    }

    if (BigBtn(b2, 'n', "Play vs Bot", "Easy / Medium / Hard", false)) {
        curScreen = SCR_BOTSETUP;
    }

    if (BigBtn(b3, 'r', "Multiplayer", "Host or join online", false)) {
        if (!gAuth.logged_in) {
            showLoginGate = true;
        } else {
            mp_reset_state();
            curScreen = SCR_MPSETUP;
        }
    }

    {
        bool sfOk = FileExists(SF_PATH);
        if (BigBtn(b4, 'Q', "Puzzles", sfOk ? "Solve checkmate puzzles" : "Stockfish not found!", !sfOk)) {
            // porneste direct un puzzle aleator (fara meniul de selectie)
            int idx = GetRandomValue(0, gPuzzleCount - 1);
            start_puzzle(idx);
        }
    }

    // === statistici jos ===
    load_stats();
    char statBuf[160];
    snprintf(statBuf, sizeof(statBuf), "Wins: %d   |   Losses: %d   |   Win Streak: %d", pWins, pLosses, pStreak);
    Vector2 stv = MeasureTextEx(gFont, statBuf, 18, 1);
    DrawTextEx(gFont, statBuf, (Vector2){ (WIN_W - stv.x) * 0.5f, WIN_H - 48.0f }, 18, 1, GOLD);

    // mic footer
    const char *foot = "UPT  -  Chess Game";
    Vector2 fv = MeasureTextEx(gFont, foot, 12, 1);
    DrawTextEx(gFont, foot, (Vector2){ (WIN_W - fv.x) * 0.5f, WIN_H - 22.0f }, 12, 1, (Color){120, 120, 120, 255});

    // === overlay: prompt de login inainte de multiplayer ===
    if (showLoginGate) {
        DrawRectangle(0, 0, WIN_W, WIN_H, (Color){0, 0, 0, 170});

        float dw = 460.0f, dh = 220.0f;
        float dx = (WIN_W - dw) * 0.5f, dy = (WIN_H - dh) * 0.5f;
        DrawRectangleRounded((Rectangle){ dx, dy, dw, dh }, 0.12f, 8, (Color){50, 60, 52, 255});
        DrawRectangleRoundedLines((Rectangle){ dx, dy, dw, dh }, 0.12f, 8, (Color){90, 130, 100, 255});

        const char *gt = "Sign in to play ranked";
        Vector2 gv = MeasureTextEx(gFont, gt, 26, 1);
        DrawTextEx(gFont, gt, (Vector2){ dx + (dw - gv.x) * 0.5f, dy + 28 }, 26, 1, WHITE);

        const char *gs = "Online matches update your ELO rank.";
        Vector2 gsv = MeasureTextEx(gFont, gs, 16, 1);
        DrawTextEx(gFont, gs, (Vector2){ dx + (dw - gsv.x) * 0.5f, dy + 70 }, 16, 1, LIGHTGRAY);

        float ibw = 180.0f, ibh = 44.0f, igap = 14.0f;
        float startX = dx + (dw - (ibw * 2 + igap)) * 0.5f;
        Rectangle bLogin = { startX, dy + dh - 70, ibw, ibh };
        Rectangle bGuest = { startX + ibw + igap, dy + dh - 70, ibw, ibh };

        if (Btn(bLogin, "Log In", false)) {
            showLoginGate = false;
            curScreen = SCR_LOGIN;
        }
        if (Btn(bGuest, "Play as Guest", false)) {
            showLoginGate = false;
            mp_reset_state();
            curScreen = SCR_MPSETUP;
        }

        // ESC inchide overlay-ul
        if (IsKeyPressed(KEY_ESCAPE)) showLoginGate = false;
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
        gTimerEnabled = false;
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
        gTimerEnabled = false;
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
        gTimerEnabled = false;
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

// helper: deseneaza fundalul "scaler" al meniurilor
static void draw_menu_bg(void)
{
    ClearBackground(C_BG);
    int tw = WIN_W / 12, th = WIN_H / 10;
    for (int rr = 0; rr < 10; rr++)
        for (int cc = 0; cc < 12; cc++) {
            unsigned char v = (rr + cc) % 2 == 0 ? 30 : 24;
            DrawRectangle(cc * tw, rr * th, tw + 1, th + 1, (Color){v, v, v, 255});
        }
}

// drenarea oricaror mesaje aparute pe bridge in timpul ecranelor de meniu
static void mp_drain_menu_messages(void)
{
    NetMsg m;
    while (net_running() && net_poll(&m)) {
        if (m.type == NM_CREATED) {
            strncpy(mpRoomCode, m.code, sizeof(mpRoomCode) - 1);
            mpRoomCode[sizeof(mpRoomCode) - 1] = '\0';
            mpMyColor = 0; // gazda joaca cu albul
            mpHosting = false;
            mpStatus[0] = '\0';
            curScreen = SCR_MPLOBBY;
        } else if (m.type == NM_JOINED) {
            strncpy(mpRoomCode, m.code, sizeof(mpRoomCode) - 1);
            mpRoomCode[sizeof(mpRoomCode) - 1] = '\0';
            mpMyColor = 1; // invitatul joaca cu negrul
            mpStatus[0] = '\0';
            if (m.opponent[0])
                snprintf(mpOpponent, sizeof(mpOpponent), "%s", m.opponent);
            // jocul incepe la primirea mesajului 'start'
        } else if (m.type == NM_START) {
            if (m.opponent[0])
                snprintf(mpOpponent, sizeof(mpOpponent), "%s", m.opponent);
            mp_begin_game(m.time_seconds);
        } else if (m.type == NM_ERROR) {
            snprintf(mpStatus, sizeof(mpStatus), "Error: %s", m.msg);
        } else if (m.type == NM_OPP_LEFT || m.type == NM_CLOSED) {
            snprintf(mpStatus, sizeof(mpStatus), "Disconnected from server");
            net_stop();
        }
    }
}

// ecranul Host/Join pentru multiplayer
void DrawMpSetup(void)
{
    draw_menu_bg();
    Vector2 mouse = GetMousePosition();

    const char *title = "ONLINE MULTIPLAYER";
    int titleSize = 56;
    Vector2 tv = MeasureTextEx(gFont, title, titleSize, 2);
    DrawTextEx(gFont, title, (Vector2){ (WIN_W - tv.x) * 0.5f, 60.0f }, titleSize, 2, WHITE);

    const char *sub = "Host a game or join with a room code";
    Vector2 sv = MeasureTextEx(gFont, sub, 20, 1);
    DrawTextEx(gFont, sub, (Vector2){ (WIN_W - sv.x) * 0.5f, 145.0f }, 20, 1, LIGHTGRAY);

    // pompeaza mesajele asincrone de la bridge
    mp_drain_menu_messages();

    float bw = 380.0f, bh = 58.0f, bx = (WIN_W - bw) * 0.5f;

    // HOST
    Rectangle bHost = { bx, 200, bw, bh };
    bool hosting = mpHosting;  // dezactivat in timp ce asteptam codul
    if (Btn(bHost, hosting ? "Connecting..." : "Host New Game", hosting)) {
        // gazda alege ceasul inainte de a deschide camera
        gTimeNext = TIME_NEXT_MP_HOST;
        curScreen = SCR_TIMESETUP;
    }

    // sectiunea JOIN
    const char *jHdr = "Join an existing room:";
    Vector2 jhv = MeasureTextEx(gFont, jHdr, 20, 1);
    DrawTextEx(gFont, jHdr, (Vector2){ (WIN_W - jhv.x) * 0.5f, 290.0f }, 20, 1, LIGHTGRAY);

    // input pentru codul camerei (4 caractere)
    Rectangle inputR = { bx, 320, bw, bh };
    bool inputHov = CheckCollisionPointRec(mouse, inputR);
    DrawRectangleRounded(inputR, 0.18f, 8, (Color){ 35, 35, 35, 255 });
    DrawRectangleRoundedLines(inputR, 0.18f, 8, inputHov ? LIME : DARKGRAY);

    // text afisat in input (cu padding) sau placeholder
    const char *placeholder = "Type room code (e.g. ABCD)";
    const char *display = (mpJoinInput[0] != '\0') ? mpJoinInput : placeholder;
    Color tc = (mpJoinInput[0] != '\0') ? WHITE : GRAY;
    Vector2 dv = MeasureTextEx(gFont, display, 28, 2);
    DrawTextEx(gFont, display,
               (Vector2){ inputR.x + (inputR.width - dv.x) * 0.5f,
                          inputR.y + (inputR.height - dv.y) * 0.5f },
               28, 2, tc);

    // input din tastatura: alfanumeric, max 4 chars (uppercase)
    int ch = GetCharPressed();
    while (ch > 0) {
        int len = (int)strlen(mpJoinInput);
        if (len < 4 && isalnum(ch)) {
            mpJoinInput[len]   = (char)toupper(ch);
            mpJoinInput[len+1] = '\0';
        }
        ch = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        int len = (int)strlen(mpJoinInput);
        if (len > 0) mpJoinInput[len-1] = '\0';
    }

    // butonul Join (activ doar cand avem 4 caractere)
    bool joinReady = (strlen(mpJoinInput) == 4);
    Rectangle bJoin = { bx, 395, bw, bh };
    if (Btn(bJoin, "Join Room", !joinReady)) {
        if (!net_running()) {
            if (!net_start(NULL)) {
                snprintf(mpStatus, sizeof(mpStatus), "Could not start network bridge");
            }
        }
        if (net_running()) {
            net_send_join(mpJoinInput, gAuth.logged_in ? gAuth.username : NULL);
            mpStatus[0] = '\0';
        }
    }

    // Enter face Join daca codul e complet
    if (joinReady && IsKeyPressed(KEY_ENTER)) {
        if (!net_running()) net_start(NULL);
        if (net_running()) {
            net_send_join(mpJoinInput, gAuth.logged_in ? gAuth.username : NULL);
            mpStatus[0] = '\0';
        }
    }

    // mesajul de stare (erori sau info)
    if (mpStatus[0] != '\0') {
        Vector2 mv = MeasureTextEx(gFont, mpStatus, 18, 1);
        DrawTextEx(gFont, mpStatus, (Vector2){ (WIN_W - mv.x) * 0.5f, 475.0f }, 18, 1, GOLD);
    }

    // Back
    Rectangle bBack = { bx, 540, bw, bh };
    if (Btn(bBack, "Back", false)) {
        mp_cleanup();
        curScreen = SCR_HOME;
    }
}

// ecranul de asteptare (gazda asteapta ca cineva sa se alature)
void DrawMpLobby(void)
{
    draw_menu_bg();

    const char *title = "WAITING FOR OPPONENT";
    int titleSize = 50;
    Vector2 tv = MeasureTextEx(gFont, title, titleSize, 2);
    DrawTextEx(gFont, title, (Vector2){ (WIN_W - tv.x) * 0.5f, 80.0f }, titleSize, 2, WHITE);

    const char *sub = "Share this code with your opponent:";
    Vector2 sv = MeasureTextEx(gFont, sub, 22, 1);
    DrawTextEx(gFont, sub, (Vector2){ (WIN_W - sv.x) * 0.5f, 175.0f }, 22, 1, LIGHTGRAY);

    // afiseaza codul mare si centrat (sau "creating..." daca inca asteptam)
    const char *codeShown = mpRoomCode[0] ? mpRoomCode : "----";
    Vector2 cv = MeasureTextEx(gFont, codeShown, 110, 6);
    DrawTextEx(gFont, codeShown,
               (Vector2){ (WIN_W - cv.x) * 0.5f, 230.0f }, 110, 6,
               mpRoomCode[0] ? GOLD : DARKGRAY);

    // animatie cu puncte
    int dots = ((int)(GetTime() * 3.0)) % 4;
    char waitTxt[32];
    snprintf(waitTxt, sizeof(waitTxt), "Waiting%.*s", dots, "...");
    Vector2 wv = MeasureTextEx(gFont, waitTxt, 24, 1);
    DrawTextEx(gFont, waitTxt, (Vector2){ (WIN_W - wv.x) * 0.5f, 380.0f }, 24, 1, LIGHTGRAY);

    // pompeaza mesajele asincrone (cand vine 'start' incepe jocul)
    mp_drain_menu_messages();

    if (mpStatus[0] != '\0') {
        Vector2 mv = MeasureTextEx(gFont, mpStatus, 18, 1);
        DrawTextEx(gFont, mpStatus, (Vector2){ (WIN_W - mv.x) * 0.5f, 440.0f }, 18, 1, RED);
    }

    float bw = 380.0f, bh = 58.0f, bx = (WIN_W - bw) * 0.5f;
    Rectangle bCancel = { bx, 540, bw, bh };
    if (Btn(bCancel, "Cancel", false)) {
        mp_cleanup();
        curScreen = SCR_HOME;
    }
}

// ecranul de selectare a puzzle-urilor
void DrawPuzzleSetup(void)
{
    draw_menu_bg();
    Vector2 mouse = GetMousePosition();

    const char *title = "CHESS PUZZLES";
    int titleSize = 60;
    Vector2 tv = MeasureTextEx(gFont, title, titleSize, 2);
    DrawTextEx(gFont, title, (Vector2){ (WIN_W - tv.x) * 0.5f, 50.0f }, titleSize, 2, WHITE);

    const char *sub = "Find the fastest checkmate";
    Vector2 sv = MeasureTextEx(gFont, sub, 20, 1);
    DrawTextEx(gFont, sub, (Vector2){ (WIN_W - sv.x) * 0.5f, 130.0f }, 20, 1, LIGHTGRAY);

    bool sfExists = FileExists(SF_PATH);
    if (!sfExists) {
        const char *err = "Stockfish engine not found!";
        Vector2 ev = MeasureTextEx(gFont, err, 22, 1);
        DrawTextEx(gFont, err, (Vector2){ (WIN_W - ev.x) * 0.5f, 165.0f }, 22, 1, RED);

        const char *hint = "Puzzles need Stockfish to defend - expected at stockfish/src/stockfish";
        Vector2 hv = MeasureTextEx(gFont, hint, 14, 1);
        DrawTextEx(gFont, hint, (Vector2){ (WIN_W - hv.x) * 0.5f, 195.0f }, 14, 1, GRAY);
    }

    // lista cu puzzle-urile - cate un card pe rand
    float cw = 620.0f, ch = 64.0f;
    float gap = 12.0f;
    float cx = (WIN_W - cw) * 0.5f;
    float cyTop = 230.0f;

    for (int i = 0; i < gPuzzleCount; i++) {
        Rectangle r = { cx, cyTop + i * (ch + gap), cw, ch };
        bool hov = sfExists && CheckCollisionPointRec(mouse, r);

        // umbra
        DrawRectangleRounded((Rectangle){ r.x + 2, r.y + 3, r.width, r.height }, 0.18f, 8, (Color){0, 0, 0, 100});

        Color bg = !sfExists ? C_BTN_DIS : (hov ? C_BTN_HOV : (Color){ 44, 60, 48, 255 });
        Color border = !sfExists ? DARKGRAY : (hov ? LIME : (Color){ 80, 120, 90, 255 });
        DrawRectangleRounded(r, 0.18f, 8, bg);
        DrawRectangleRoundedLines(r, 0.18f, 8, border);

        // numar puzzle (cerc cu index)
        DrawCircle((int)(r.x + 30), (int)(r.y + r.height * 0.5f), 18, (Color){ 30, 30, 30, 255 });
        char idxBuf[4];
        snprintf(idxBuf, sizeof(idxBuf), "%d", i + 1);
        Vector2 iv = MeasureTextEx(gFont, idxBuf, 22, 1);
        DrawTextEx(gFont, idxBuf,
                   (Vector2){ r.x + 30 - iv.x * 0.5f, r.y + r.height * 0.5f - iv.y * 0.5f },
                   22, 1, GOLD);

        // numele puzzle-ului
        DrawTextEx(gFont, gPuzzles[i].name, (Vector2){ r.x + 64, r.y + 8 }, 22, 1, WHITE);

        // descriere
        DrawTextEx(gFont, gPuzzles[i].desc, (Vector2){ r.x + 64, r.y + 36 }, 14, 1, (Color){200, 220, 200, 230});

        // eticheta "Mate in N"
        char mateBuf[24];
        snprintf(mateBuf, sizeof(mateBuf), "Mate in %d", gPuzzles[i].target);
        Vector2 mv = MeasureTextEx(gFont, mateBuf, 18, 1);
        DrawTextEx(gFont, mateBuf,
                   (Vector2){ r.x + r.width - mv.x - 18.0f, r.y + r.height * 0.5f - mv.y * 0.5f },
                   18, 1, GOLD);

        if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            start_puzzle(i);
        }
    }

    // Buton Back
    float bw = 380.0f, bh = 50.0f, bx = (WIN_W - bw) * 0.5f;
    Rectangle bBack = { bx, WIN_H - 70.0f, bw, bh };
    if (Btn(bBack, "Back", false)) {
        curScreen = SCR_HOME;
    }
}

// deseneaza un input field rotunjit cu placeholder + cursor; updateaza 'buf'
// in functie de tastele apasate cand 'focused' este true. 'mask' afiseaza '*'.
static void TextInput(Rectangle r, char *buf, int buf_sz, const char *placeholder,
                      bool mask, bool focused, bool *clicked_into)
{
    Vector2 mouse = GetMousePosition();
    bool hov = CheckCollisionPointRec(mouse, r);
    Color border = focused ? LIME : (hov ? (Color){120, 180, 130, 255} : DARKGRAY);

    DrawRectangleRounded(r, 0.18f, 8, (Color){ 35, 35, 35, 255 });
    DrawRectangleRoundedLines(r, 0.18f, 8, border);

    // text afisat (cu mascare daca e parola)
    char shown[128];
    int len = (int)strlen(buf);
    if (mask) {
        int n = len < (int)sizeof(shown) - 1 ? len : (int)sizeof(shown) - 1;
        for (int i = 0; i < n; i++) shown[i] = '*';
        shown[n] = '\0';
    } else {
        snprintf(shown, sizeof(shown), "%s", buf);
    }

    const char *display = (shown[0] != '\0') ? shown : placeholder;
    Color tc = (shown[0] != '\0') ? WHITE : (Color){120, 120, 120, 255};
    Vector2 dv = MeasureTextEx(gFont, display, 22, 1);
    float tx = r.x + 18.0f;
    float ty = r.y + (r.height - dv.y) * 0.5f;
    DrawTextEx(gFont, display, (Vector2){ tx, ty }, 22, 1, tc);

    // cursor cand suntem focusati
    if (focused) {
        float caretX = tx + (shown[0] != '\0' ? MeasureTextEx(gFont, shown, 22, 1).x : 0);
        if (((int)(GetTime() * 2)) % 2 == 0) {
            DrawRectangle((int)(caretX + 2), (int)(r.y + 12), 2, (int)(r.height - 24), LIME);
        }
        // input din tastatura
        int ch = GetCharPressed();
        while (ch > 0) {
            int curLen = (int)strlen(buf);
            // pentru username acceptam doar alfanumeric + underscore
            // pentru password acceptam orice imprimabil
            bool accept;
            if (mask) accept = (ch >= 32 && ch < 127);
            else      accept = (isalnum(ch) || ch == '_');
            if (accept && curLen < buf_sz - 1) {
                buf[curLen] = (char)ch;
                buf[curLen + 1] = '\0';
            }
            ch = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            int curLen = (int)strlen(buf);
            if (curLen > 0) buf[curLen - 1] = '\0';
        }
    }

    if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        if (clicked_into) *clicked_into = true;
    }
}

// ecran de autentificare: login + register
void DrawLogin(void)
{
    draw_menu_bg();

    const char *title = "ACCOUNT";
    int titleSize = 60;
    Vector2 tv = MeasureTextEx(gFont, title, titleSize, 2);
    DrawTextEx(gFont, title, (Vector2){ (WIN_W - tv.x) * 0.5f, 60.0f }, titleSize, 2, WHITE);

    const char *sub = "Login or register to track your rank";
    Vector2 sv = MeasureTextEx(gFont, sub, 20, 1);
    DrawTextEx(gFont, sub, (Vector2){ (WIN_W - sv.x) * 0.5f, 145.0f }, 20, 1, LIGHTGRAY);

    float bw = 380.0f, bh = 50.0f, bx = (WIN_W - bw) * 0.5f;

    // etichete + input fields
    DrawTextEx(gFont, "Username", (Vector2){ bx, 195.0f }, 16, 1, LIGHTGRAY);
    Rectangle inU = { bx, 220.0f, bw, bh };
    bool clickU = false;
    TextInput(inU, loginUser, sizeof(loginUser), "your username", false, loginFocus == 0, &clickU);
    if (clickU) loginFocus = 0;

    DrawTextEx(gFont, "Password", (Vector2){ bx, 290.0f }, 16, 1, LIGHTGRAY);
    Rectangle inP = { bx, 315.0f, bw, bh };
    bool clickP = false;
    TextInput(inP, loginPass, sizeof(loginPass), "your password", true, loginFocus == 1, &clickP);
    if (clickP) loginFocus = 1;

    // TAB pentru a comuta focus
    if (IsKeyPressed(KEY_TAB)) loginFocus = 1 - loginFocus;

    // butoanele Login si Register pe acelasi rand
    float gap = 16.0f;
    float halfW = (bw - gap) * 0.5f;
    Rectangle bLogin = { bx, 395.0f, halfW, bh };
    Rectangle bReg   = { bx + halfW + gap, 395.0f, halfW, bh };

    bool canSubmit = !loginBusy && loginUser[0] && loginPass[0];

    bool doLogin = false, doRegister = false;
    if (Btn(bLogin, loginBusy ? "..." : "Log In",   !canSubmit)) doLogin    = true;
    if (Btn(bReg,   loginBusy ? "..." : "Register", !canSubmit)) doRegister = true;
    (void)bLogin; (void)bReg;
    // Enter ca shortcut pentru Log In
    if (canSubmit && IsKeyPressed(KEY_ENTER)) doLogin = true;

    if (doLogin) {
        loginBusy = true;
        char err[AUTH_ERR_MAX] = {0};
        int ok = auth_login(loginUser, loginPass, err, sizeof(err));
        loginBusy = false;
        if (ok) {
            snprintf(loginStatus, sizeof(loginStatus), "Welcome, %s!", gAuth.username);
            loginStatusColor = (Color){ 130, 220, 130, 255 };
            loginPass[0] = '\0';
            // dupa login, mergi inapoi la home
            curScreen = SCR_HOME;
        } else {
            snprintf(loginStatus, sizeof(loginStatus), "%s", err[0] ? err : "Login failed");
            loginStatusColor = (Color){ 220, 80, 80, 255 };
        }
    }

    if (doRegister) {
        loginBusy = true;
        char err[AUTH_ERR_MAX] = {0};
        int ok = auth_register(loginUser, loginPass, err, sizeof(err));
        if (ok) {
            // dupa register, login automat
            ok = auth_login(loginUser, loginPass, err, sizeof(err));
        }
        loginBusy = false;
        if (ok) {
            snprintf(loginStatus, sizeof(loginStatus), "Account created. Welcome, %s!", gAuth.username);
            loginStatusColor = (Color){ 130, 220, 130, 255 };
            loginPass[0] = '\0';
            curScreen = SCR_HOME;
        } else {
            snprintf(loginStatus, sizeof(loginStatus), "%s", err[0] ? err : "Registration failed");
            loginStatusColor = (Color){ 220, 80, 80, 255 };
        }
    }

    // mesajul de status
    if (loginStatus[0]) {
        Vector2 mv = MeasureTextEx(gFont, loginStatus, 16, 1);
        DrawTextEx(gFont, loginStatus, (Vector2){ (WIN_W - mv.x) * 0.5f, 465.0f }, 16, 1, loginStatusColor);
    }

    // server URL hint (mic, jos)
    char hint[280];
    snprintf(hint, sizeof(hint), "Server: %s", auth_server_url());
    Vector2 hv = MeasureTextEx(gFont, hint, 12, 1);
    DrawTextEx(gFont, hint, (Vector2){ (WIN_W - hv.x) * 0.5f, WIN_H - 90.0f }, 12, 1, GRAY);

    // Back
    Rectangle bBack = { bx, WIN_H - 70.0f, bw, 45 };
    if (Btn(bBack, "Back", false)) {
        loginStatus[0] = '\0';
        loginPass[0] = '\0';
        curScreen = SCR_HOME;
    }
}

// ecran profil cu rank si statistici
void DrawProfile(void)
{
    draw_menu_bg();

    if (!gAuth.logged_in) {
        // nu ar trebui sa ajungem aici, dar fallback
        curScreen = SCR_LOGIN;
        return;
    }

    // header cu titlu
    const char *title = "PROFILE";
    int titleSize = 56;
    Vector2 tv = MeasureTextEx(gFont, title, titleSize, 2);
    DrawTextEx(gFont, title, (Vector2){ (WIN_W - tv.x) * 0.5f, 50.0f }, titleSize, 2, WHITE);

    // card mare cu username + rank
    float cw = 560.0f, ch = 200.0f;
    float cx = (WIN_W - cw) * 0.5f;
    float cy = 140.0f;
    DrawRectangleRounded((Rectangle){ cx + 3, cy + 4, cw, ch }, 0.12f, 8, (Color){0, 0, 0, 110});
    DrawRectangleRounded((Rectangle){ cx, cy, cw, ch }, 0.12f, 8, (Color){ 40, 50, 42, 255 });
    DrawRectangleRoundedLines((Rectangle){ cx, cy, cw, ch }, 0.12f, 8, (Color){ 90, 130, 100, 255 });

    // username
    DrawTextEx(gFont, gAuth.username, (Vector2){ cx + 30, cy + 22 }, 34, 1, WHITE);

    // titlu ELO la dreapta
    const char *rlbl = "RANK";
    Vector2 rlv = MeasureTextEx(gFont, rlbl, 14, 2);
    DrawTextEx(gFont, rlbl, (Vector2){ cx + cw - rlv.x - 30, cy + 18 }, 14, 2, (Color){180, 180, 180, 255});

    char rankBuf[16];
    snprintf(rankBuf, sizeof(rankBuf), "%d", gAuth.rank);
    Vector2 rv = MeasureTextEx(gFont, rankBuf, 60, 1);
    DrawTextEx(gFont, rankBuf, (Vector2){ cx + cw - rv.x - 30, cy + 36 }, 60, 1, GOLD);

    // separator
    DrawLine((int)(cx + 30), (int)(cy + 110), (int)(cx + cw - 30), (int)(cy + 110),
             (Color){80, 100, 85, 255});

    // statistici W/L/T
    const char *labels[3] = { "WINS", "LOSSES", "TIES" };
    int values[3] = { gAuth.wins, gAuth.losses, gAuth.ties };
    Color valC[3] = {
        (Color){130, 220, 130, 255}, (Color){220, 90, 90, 255}, (Color){200, 200, 200, 255}
    };
    float colW = (cw - 60) / 3.0f;
    for (int i = 0; i < 3; i++) {
        float colX = cx + 30 + i * colW;
        char vbuf[16];
        snprintf(vbuf, sizeof(vbuf), "%d", values[i]);
        Vector2 vv = MeasureTextEx(gFont, vbuf, 36, 1);
        DrawTextEx(gFont, vbuf, (Vector2){ colX + (colW - vv.x) * 0.5f, cy + 125 }, 36, 1, valC[i]);
        Vector2 lv = MeasureTextEx(gFont, labels[i], 13, 2);
        DrawTextEx(gFont, labels[i], (Vector2){ colX + (colW - lv.x) * 0.5f, cy + 168 }, 13, 2, GRAY);
    }

    // === card abonament ===
    float scy = cy + ch + 14.0f;
    float scw = cw, sch = 90.0f;
    float scx = cx;

    bool subPro        = strcmp(gAuth.subscription, "pro")        == 0;
    bool subCancelling = strcmp(gAuth.subscription, "cancelling") == 0;

    Color scBorder = subPro        ? (Color){ 200, 160, 40, 255 } :
                     subCancelling ? (Color){ 200, 130, 40, 255 } :
                                     (Color){  70,  70,  70, 200 };
    Color scBg     = subPro        ? (Color){  48,  40,  16, 255 } :
                     subCancelling ? (Color){  48,  36,  14, 255 } :
                                     (Color){  36,  36,  36, 235 };

    DrawRectangleRounded((Rectangle){ scx + 3, scy + 4, scw, sch }, 0.12f, 8,
                         (Color){ 0, 0, 0, 110 });
    DrawRectangleRounded((Rectangle){ scx, scy, scw, sch }, 0.12f, 8, scBg);
    DrawRectangleRoundedLines((Rectangle){ scx, scy, scw, sch }, 0.12f, 8, scBorder);

    // titlu card
    Color scTitleC = (subPro || subCancelling) ? GOLD : (Color){ 130, 130, 130, 255 };
    DrawTextEx(gFont, "AI COACH PRO", (Vector2){ scx + 24, scy + 14 }, 20, 1, scTitleC);

    // separator interior
    DrawLine((int)(scx + 24), (int)(scy + 42), (int)(scx + scw - 24), (int)(scy + 42),
             (Color){ 80, 80, 80, 180 });

    // descriere status
    const char *scDesc = subPro        ? "Subscription active. AI coaching features are enabled." :
                         subCancelling ? "Cancelled. Access continues until the billing period ends." :
                                         "Subscribe at the web app to unlock AI Coach Pro.";
    Color scDescC = (subPro || subCancelling) ? (Color){ 180, 180, 180, 255 }
                                               : (Color){ 110, 110, 110, 255 };
    DrawTextEx(gFont, scDesc, (Vector2){ scx + 24, scy + 52 }, 14, 1, scDescC);

    // badge status (sus-dreapta in card)
    if (subPro || subCancelling) {
        const char *badgeLbl = subPro ? "ACTIVE" : "CANCELLING";
        Color badgeBg   = subPro ? (Color){ 35, 110, 45, 230 } : (Color){ 150, 90, 15, 230 };
        Color badgeTxtC = (Color){ 220, 255, 220, 255 };
        Vector2 blv = MeasureTextEx(gFont, badgeLbl, 12, 1);
        float bpad = 9.0f;
        Rectangle bRect = { scx + scw - blv.x - bpad * 2 - 20, scy + 12,
                            blv.x + bpad * 2, blv.y + 6 };
        DrawRectangleRounded(bRect, 0.5f, 8, badgeBg);
        DrawTextEx(gFont, badgeLbl,
                   (Vector2){ bRect.x + bpad, bRect.y + 3 },
                   12, 1, badgeTxtC);
    }

    // butoane jos
    float bw = 240.0f, bh = 50.0f;
    float byTop = scy + sch + 16.0f;
    float bxL = (WIN_W - (bw * 2 + 20)) * 0.5f;

    Rectangle bRefresh = { bxL, byTop, bw, bh };
    if (Btn(bRefresh, "Refresh", false)) {
        char err[AUTH_ERR_MAX] = {0};
        auth_refresh_profile(err, sizeof(err));
        if (err[0]) {
            snprintf(loginStatus, sizeof(loginStatus), "%s", err);
            loginStatusColor = (Color){ 220, 80, 80, 255 };
        }
    }

    Rectangle bLogout = { bxL + bw + 20, byTop, bw, bh };
    if (Btn(bLogout, "Log Out", false)) {
        auth_logout();
        curScreen = SCR_HOME;
    }

    // buton gestionare abonament — deschide pagina de cont in browser
    float mbY = byTop + bh + 12.0f;
    float mbW = bw * 2 + 20.0f;
    float mbH = 44.0f;
    Rectangle bManage = { bxL, mbY, mbW, mbH };
    const char *manageLbl = (subPro || subCancelling)
                            ? "Manage Subscription  ->"
                            : "Subscribe to AI Coach Pro  ->";
    if (BtnGold(bManage, manageLbl, false)) {
        char accountUrl[512];
        snprintf(accountUrl, sizeof(accountUrl), "%s/account", frontend_url());
        open_in_browser(accountUrl);
    }

    // status (eg eroare refresh)
    if (loginStatus[0]) {
        Vector2 mv = MeasureTextEx(gFont, loginStatus, 14, 1);
        DrawTextEx(gFont, loginStatus,
                   (Vector2){ (WIN_W - mv.x) * 0.5f, mbY + mbH + 14 },
                   14, 1, loginStatusColor);
    }

    // Back
    Rectangle bBack = { (WIN_W - 380.0f) * 0.5f, WIN_H - 70.0f, 380.0f, 45 };
    if (Btn(bBack, "Back to Menu", false)) {
        loginStatus[0] = '\0';
        curScreen = SCR_HOME;
    }
}

// porneste un joc local 1v1 cu un timer dat (in secunde, 0 = fara timer)
static void start_local_1v1(int time_seconds)
{
    botMode = 0;
    mpMode = 0;
    puzzleMode = 0;
    init_board();
    current_turn = 0;
    selRow = selCol = -1;
    hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
    gameSt = ST_SELECT;

    gTimerEnabled = (time_seconds > 0);
    gTimeInitial  = time_seconds;
    gWhiteTime    = (double)time_seconds;
    gBlackTime    = (double)time_seconds;
    gLastTickT    = GetTime();
    gFlagged      = -1;

    curScreen = SCR_GAME;
}

// ecran de alegere a ceasului (1 / 5 / 10 minute) — folosit atat pentru 1v1
// cat si pentru gazda multiplayer.
void DrawTimeSetup(void)
{
    draw_menu_bg();

    const char *title = "PICK A TIME CONTROL";
    int titleSize = 50;
    Vector2 tv = MeasureTextEx(gFont, title, titleSize, 2);
    DrawTextEx(gFont, title, (Vector2){ (WIN_W - tv.x) * 0.5f, 70.0f }, titleSize, 2, WHITE);

    const char *sub = (gTimeNext == TIME_NEXT_MP_HOST)
        ? "The guest will play with your choice"
        : "How long does each side get on the clock?";
    Vector2 sv = MeasureTextEx(gFont, sub, 20, 1);
    DrawTextEx(gFont, sub, (Vector2){ (WIN_W - sv.x) * 0.5f, 150.0f }, 20, 1, LIGHTGRAY);

    static const int times[3]      = { 60, 300, 600 };
    static const char *labels[3]   = { "1 min", "5 min", "10 min" };
    static const char *blurbs[3]   = { "Bullet — fast and chaotic",
                                       "Blitz — the classic",
                                       "Rapid — time to think" };

    float bw = 420.0f, bh = 90.0f, gap = 18.0f;
    float bx = (WIN_W - bw) * 0.5f;
    float byTop = 220.0f;

    Vector2 mouse = GetMousePosition();

    for (int i = 0; i < 3; i++) {
        Rectangle r = { bx, byTop + i * (bh + gap), bw, bh };
        bool hov = CheckCollisionPointRec(mouse, r);

        DrawRectangleRounded((Rectangle){ r.x + 2, r.y + 3, r.width, r.height }, 0.18f, 8, (Color){0, 0, 0, 110});
        Color bg = hov ? C_BTN_HOV : C_BTN;
        Color border = hov ? LIME : GREEN;
        DrawRectangleRounded(r, 0.18f, 8, bg);
        DrawRectangleRoundedLines(r, 0.18f, 8, border);

        // icon ceas (cerc cu cruce subtila)
        DrawCircle((int)(r.x + 50), (int)(r.y + r.height * 0.5f), 24, (Color){25, 30, 26, 255});
        DrawCircleLines((int)(r.x + 50), (int)(r.y + r.height * 0.5f), 24, GOLD);
        // ace
        DrawLineEx((Vector2){ r.x + 50, r.y + r.height * 0.5f },
                   (Vector2){ r.x + 50, r.y + r.height * 0.5f - 14 }, 2.0f, GOLD);
        DrawLineEx((Vector2){ r.x + 50, r.y + r.height * 0.5f },
                   (Vector2){ r.x + 50 + 10, r.y + r.height * 0.5f }, 2.0f, GOLD);

        // labels
        DrawTextEx(gFont, labels[i], (Vector2){ r.x + 100, r.y + 20 }, 30, 1, WHITE);
        DrawTextEx(gFont, blurbs[i], (Vector2){ r.x + 100, r.y + 56 }, 16, 1,
                   (Color){200, 220, 200, 230});

        if (hov && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            int secs = times[i];
            if (gTimeNext == TIME_NEXT_LOCAL_1V1) {
                start_local_1v1(secs);
            } else {
                // multiplayer host: porneste podul si trimite create cu timpul ales
                if (!net_running()) {
                    if (!net_start(NULL)) {
                        snprintf(mpStatus, sizeof(mpStatus), "Could not start network bridge");
                        curScreen = SCR_MPSETUP;
                        return;
                    }
                }
                if (net_running()) {
                    mpHosting = true;
                    net_send_create(gAuth.logged_in ? gAuth.username : NULL, secs);
                    mpStatus[0] = '\0';
                    curScreen = SCR_MPLOBBY;
                }
            }
        }
    }

    // Back
    Rectangle bBack = { bx, WIN_H - 70.0f, bw, 45 };
    if (Btn(bBack, "Back", false)) {
        curScreen = (gTimeNext == TIME_NEXT_MP_HOST) ? SCR_MPSETUP : SCR_HOME;
    }
}

//ecran de joc
void DrawGame(void)
{
    ClearBackground(C_BG);
    Vector2 mouse = GetMousePosition();

    /* ── timer: decrementeaza ceasul jucatorului activ ── */
    double now = GetTime();
    if (gTimerEnabled) {
        double dt = now - gLastTickT;
        if (dt < 0) dt = 0;
        // ticaim doar in starile in care ceasul ruleaza efectiv
        bool ticking = (gameSt == ST_SELECT || gameSt == ST_WAIT_OPP);
        if (ticking) {
            if (current_turn == 0) gWhiteTime -= dt;
            else                   gBlackTime -= dt;
        }

        // detecteaza timeout
        if (gameSt != ST_GAMEOVER) {
            if (gWhiteTime <= 0.0 || gBlackTime <= 0.0) {
                if (gWhiteTime <= 0.0) gWhiteTime = 0.0;
                if (gBlackTime <= 0.0) gBlackTime = 0.0;
                int flagged = (gWhiteTime <= 0.0) ? 0 : 1;
                gFlagged = flagged;
                if (mpMode) {
                    // jucatorul ramas fara timp pierde; daca e culoarea noastra,
                    // anuntam serverul printr-un mesaj de resign
                    if (flagged == mpMyColor) {
                        net_send_resign();
                        mp_report_result("loss");
                    } else {
                        mp_report_result("win");
                    }
                    snprintf(mpStatus, sizeof(mpStatus),
                             "Time out — %s wins", (flagged == 0) ? "Black" : "White");
                } else if (botMode) {
                    // botMode nu foloseste timer in mod normal, dar in caz ca cineva
                    // l-ar activa: tratam ca pierdere/castig dupa care a pierdut timpul
                    if (flagged == 0) { pLosses++; pStreak = 0; }
                    else              { pWins++;   pStreak++;  }
                    save_stats();
                }
                gameSt = ST_GAMEOVER;
            }
        }
    }
    gLastTickT = now;

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

    /* ── multiplayer: polling pentru mutari sau evenimente ── */
    if (mpMode) {
        NetMsg nm;
        while (net_poll(&nm)) {
            if (nm.type == NM_MOVE) {
                if (mp_apply_remote_move(nm.uci)) {
                    selRow = selCol = -1;
                    hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
                    if (!has_legal_moves(current_turn)) {
                        set_game_over();
                    } else {
                        gameSt = ST_SELECT;  // acum e randul nostru
                    }
                }
            } else if (nm.type == NM_OPP_LEFT) {
                mpOpponentLeft = true;
                snprintf(mpStatus, sizeof(mpStatus), "Opponent disconnected");
                gameSt = ST_GAMEOVER;
                mp_report_result("win");
            } else if (nm.type == NM_RESIGN) {
                snprintf(mpStatus, sizeof(mpStatus), "Opponent resigned");
                gameSt = ST_GAMEOVER;
                mp_report_result("win");
            } else if (nm.type == NM_ERROR) {
                snprintf(mpStatus, sizeof(mpStatus), "Network: %s", nm.msg);
            } else if (nm.type == NM_CLOSED) {
                mpOpponentLeft = true;
                snprintf(mpStatus, sizeof(mpStatus), "Disconnected from server");
                gameSt = ST_GAMEOVER;
            }
        }
    }

    //calculeaza starea de sah
    bool inCheck = (gameSt != ST_GAMEOVER) && is_in_check(current_turn);

    //gaseste pozitia regelui pentru a evidentia ca e in sah
    int kingR = -1, kingC = -1;
    if (inCheck) get_king_pos(current_turn, &kingR, &kingC);

    //tabla
    bool flipped = board_flipped();

    //umbra tablei
    DrawRectangle(BOARD_X + 4, BOARD_Y + 4, BOARD_PX, BOARD_PX, (Color){0, 0, 0, 110});

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            // dr/dc = coordonatele vizuale (pe ecran), r/c = coordonatele logice (din matrice)
            int dr = flipped ? 7 - r : r;
            int dc = flipped ? 7 - c : c;
            float x = (float)(BOARD_X + dc * SQ);
            float y = (float)(BOARD_Y + dr * SQ);

            //patrat de baza (culorile tablei depind de coordonatele logice)
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

    //etichete randuri (inversate daca tabla e inversata)
    for (int r = 0; r < 8; r++) {
        int logicR = flipped ? 7 - r : r;
        char lbl[2] = { (char)('0' + (8 - logicR)), '\0' };
        DrawTextEx(gFont, lbl, (Vector2){ BOARD_X - 22.0f, BOARD_Y + r * SQ + SQ * 0.5f - 9.0f }, 18, 0, LIGHTGRAY);
    }
    //etichete coloane (inversate daca tabla e inversata)
    for (int c = 0; c < 8; c++) {
        int logicC = flipped ? 7 - c : c;
        char lbl[2] = { (char)('a' + logicC), '\0' };
        DrawTextEx(gFont, lbl, (Vector2){ BOARD_X + c * SQ + SQ * 0.5f - 6.0f, BOARD_Y + BOARD_PX + 8.0f }, 18, 0, LIGHTGRAY);
    }

    //piese (pozitionate conform inversarii tablei)
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) {
            int dr = flipped ? 7 - r : r;
            int dc = flipped ? 7 - c : c;
            DrawPiece(board[r][c], (float)(BOARD_X + dc * SQ), (float)(BOARD_Y + dr * SQ));
        }

    //panou din dreapta
    DrawRectangle(PANEL_X - 10, PANEL_Y, PANEL_W + 10, PANEL_H, C_PANEL);
    DrawRectangleLinesEx((Rectangle){ PANEL_X - 10, PANEL_Y, PANEL_W + 10, PANEL_H }, 1, DARKGRAY);

    float px = (float)(PANEL_X + 5);
    float py = (float)(PANEL_Y + 18);

    // ── ceasuri (afisate doar daca jocul are timer) ──
    if (gTimerEnabled) {
        float clockW = PANEL_W - 20.0f;
        float clockH = 56.0f;
        // negrul sus, albul jos (urmeaza orientarea tablei)
        for (int side = 0; side < 2; side++) {
            int color = (side == 0) ? 1 : 0;  // 0 = negru desenat sus, 1 = alb desenat jos
            double remaining = (color == 0) ? gWhiteTime : gBlackTime;
            bool active = (current_turn == color) && (gameSt != ST_GAMEOVER);
            bool low = remaining <= 10.0;

            float cy2 = py + side * (clockH + 6.0f);
            Rectangle cr = { px, cy2, clockW, clockH };
            Color bg = active ? (low ? (Color){80, 30, 30, 255} : (Color){38, 70, 50, 255})
                              : (Color){30, 30, 30, 255};
            Color brd = active ? (low ? (Color){220, 70, 70, 255} : LIME)
                               : (Color){70, 70, 70, 255};
            DrawRectangleRounded(cr, 0.22f, 8, bg);
            DrawRectangleRoundedLines(cr, 0.22f, 8, brd);

            // disc indicator de culoare
            Color disc = (color == 0) ? WHITE : (Color){25, 25, 25, 255};
            DrawCircle((int)(px + 22), (int)(cy2 + clockH * 0.5f), 13, LIGHTGRAY);
            DrawCircle((int)(px + 22), (int)(cy2 + clockH * 0.5f), 10, disc);

            // timpul ramas
            int total = (int)(remaining + 0.5);
            if (total < 0) total = 0;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
            Vector2 tv2 = MeasureTextEx(gFont, buf, 32, 1);
            DrawTextEx(gFont, buf,
                       (Vector2){ px + clockW - tv2.x - 16, cy2 + (clockH - tv2.y) * 0.5f },
                       32, 1, low && active ? (Color){255, 200, 200, 255} : WHITE);
        }
        py += 2 * 56.0f + 6.0f + 14.0f;

        // separator
        DrawLine((int)px, (int)py, (int)(px + PANEL_W - 20), (int)py, DARKGRAY);
        py += 12.0f;
    }

    //eticheta "turn"
    const char *turnLbl = "TURN";
    Vector2 tlv = MeasureTextEx(gFont, turnLbl, 14, 2);
    DrawTextEx(gFont, turnLbl, (Vector2){ px + (PANEL_W - tlv.x) * 0.5f, py }, 14, 2, GRAY);
    py += 26.0f;

    //mostra de culoare + numele jucatorului
    const char *who;
    if (botMode)
        who = (current_turn == 0) ? "You (White)" : "Bot (Black)";
    else if (mpMode)
        who = (current_turn == mpMyColor) ? "Your turn" : "Opponent's turn";
    else if (puzzleMode)
        who = (current_turn == puzzleUserColor) ? "Your move" : "Defender";
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

    // in modul multiplayer, afiseaza un mesaj cand asteptam mutarea oponentului
    if (gameSt == ST_WAIT_OPP) {
        int dots = ((int)(GetTime() * 3.0)) % 4;
        char waitTxt[32];
        snprintf(waitTxt, sizeof(waitTxt), "Waiting%.*s", dots, "...");
        Vector2 wv = MeasureTextEx(gFont, waitTxt, 22, 1);
        DrawTextEx(gFont, waitTxt, (Vector2){ px + (PANEL_W - wv.x) * 0.5f - 5.0f, py }, 22, 1, GOLD);
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

    // info multiplayer: oponent + rank (cand suntem logati)
    if (mpMode && gameSt != ST_GAMEOVER) {
        if (mpOpponent[0]) {
            char oppBuf[64];
            snprintf(oppBuf, sizeof(oppBuf), "vs %s", mpOpponent);
            Vector2 ov = MeasureTextEx(gFont, oppBuf, 16, 1);
            DrawTextEx(gFont, oppBuf, (Vector2){ px + (PANEL_W - ov.x) * 0.5f - 5.0f, py }, 16, 1, LIGHTGRAY);
            py += 22.0f;
        }
        if (gAuth.logged_in) {
            char rkBuf[32];
            snprintf(rkBuf, sizeof(rkBuf), "Your rank: %d", gAuth.rank);
            Vector2 rkv = MeasureTextEx(gFont, rkBuf, 16, 1);
            DrawTextEx(gFont, rkBuf, (Vector2){ px + (PANEL_W - rkv.x) * 0.5f - 5.0f, py }, 16, 1, GOLD);
            py += 22.0f;
        }
    }

    // info puzzle: numele puzzle-ului, dificultatea si numarul de mutari folosite
    if (puzzleMode && gameSt != ST_GAMEOVER) {
        Vector2 pnv = MeasureTextEx(gFont, gPuzzles[puzzleIdx].name, 16, 1);
        DrawTextEx(gFont, gPuzzles[puzzleIdx].name,
                   (Vector2){ px + (PANEL_W - pnv.x) * 0.5f - 5.0f, py },
                   16, 1, GOLD);
        py += 22.0f;

        // eticheta de dificultate cu culoare corespunzatoare
        const char *diffLabel;
        Color diffColor;
        int diff = gPuzzles[puzzleIdx].difficulty;
        if (diff <= 1)      { diffLabel = "Easy";   diffColor = (Color){100, 220, 100, 255}; }
        else if (diff == 2) { diffLabel = "Medium"; diffColor = (Color){240, 200, 60, 255};  }
        else                { diffLabel = "Hard";   diffColor = (Color){220, 80, 80, 255};   }
        char diffBuf[32];
        snprintf(diffBuf, sizeof(diffBuf), "Difficulty: %s", diffLabel);
        Vector2 dv2 = MeasureTextEx(gFont, diffBuf, 14, 1);
        DrawTextEx(gFont, diffBuf,
                   (Vector2){ px + (PANEL_W - dv2.x) * 0.5f - 5.0f, py },
                   14, 1, diffColor);
        py += 22.0f;

        char mvBuf[32];
        snprintf(mvBuf, sizeof(mvBuf), "Moves: %d / %d", puzzleMoveCount, puzzleTarget);
        Vector2 mvv = MeasureTextEx(gFont, mvBuf, 16, 1);
        DrawTextEx(gFont, mvBuf,
                   (Vector2){ px + (PANEL_W - mvv.x) * 0.5f - 5.0f, py },
                   16, 1, LIGHTGRAY);
    }

    // mesaj de stare multiplayer (erori de retea, etc.)
    if (mpMode && mpStatus[0] != '\0') {
        Vector2 mv = MeasureTextEx(gFont, mpStatus, 14, 1);
        DrawTextEx(gFont, mpStatus, (Vector2){ px + (PANEL_W - mv.x) * 0.5f - 5.0f, py }, 14, 1, GOLD);
        py += 22.0f;
    }

    // butoanele panoului
    float btnY = (float)(PANEL_Y + PANEL_H - 170);
    Rectangle rHint = { (float)PANEL_X, btnY, (float)(PANEL_W - 20), 45 };
    Rectangle rNew = { (float)PANEL_X, btnY + 57.0f, (float)(PANEL_W - 20), 45 };
    Rectangle rMenu = { (float)PANEL_X, btnY + 114.0f,(float)(PANEL_W - 20), 45 };

    if (mpMode) {
        // in multiplayer: primul buton = Resign (in loc de Suggest Move)
        bool canResign = (gameSt == ST_SELECT || gameSt == ST_WAIT_OPP);
        if (Btn(rHint, "Resign", !canResign)) {
            net_send_resign();
            snprintf(mpStatus, sizeof(mpStatus), "You resigned");
            gameSt = ST_GAMEOVER;
            mp_report_result("loss");
        }
    } else if (puzzleMode) {
        // in puzzle: buton "Skip Puzzle" care incarca un alt puzzle aleator
        if (Btn(rHint, "Skip Puzzle", false)) {
            int newIdx = puzzleIdx;
            if (gPuzzleCount > 1) {
                while (newIdx == puzzleIdx)
                    newIdx = GetRandomValue(0, gPuzzleCount - 1);
            }
            start_puzzle(newIdx);
        }
    } else {
        // in modul local/bot: butonul de hint
        if (Btn(rHint, "Suggest Move", gameSt != ST_SELECT)) {
            if (sf_pid <= 0) sf_start();
            if (sf_pid > 0) {
                gameSt = ST_HINT_THINKING;
                sf_request_move();
            }
        }
    }

    if (mpMode) {
        // in multiplayer: "New Game" nu are sens, il dezactivam
        Btn(rNew, "New Game", true);
    } else if (puzzleMode) {
        // in puzzle: butonul reseteaza puzzle-ul curent
        if (Btn(rNew, "Reset Puzzle", false)) {
            load_puzzle_position(gPuzzles[puzzleIdx].setup, gPuzzles[puzzleIdx].side_to_move);
            puzzleMoveCount = 0;
            puzzleSolved = false;
            puzzleFailed = false;
            selRow = selCol = -1;
            hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
            gameSt = ST_SELECT;
        }
    } else {
        if (Btn(rNew,  "New Game",  false)) {
            init_board();
            current_turn = 0;
            selRow = selCol = -1;
            hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
            gameSt = ST_SELECT;
            // re-armam ceasul daca jocul curent il foloseste
            if (gTimerEnabled) {
                gWhiteTime = gBlackTime = (double)gTimeInitial;
                gLastTickT = GetTime();
            }
        }
    }

    if (Btn(rMenu, "Main Menu", false)) {
        if (mpMode) {
            mp_cleanup();  // inchide conexiunea de retea
        }
        puzzleMode = 0;
        puzzleSolved = false;
        puzzleFailed = false;
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

                // trimite mutarea prin retea daca suntem in modul multiplayer
                if (mpMode) {
                    char uci[8];
                    coords_to_uci(promSrcRow, promSrcCol, promDstRow, promDstCol, pKeys[i][0], uci);
                    net_send_move(uci);
                }

                current_turn = 1 - current_turn;
                selRow = selCol = -1;

                if (puzzleMode) puzzleMoveCount++;

                if (!has_legal_moves(current_turn)) {
                    set_game_over();
                } else if (mpMode) {
                    // dupa promovare in multiplayer, asteptam mutarea oponentului
                    gameSt = ST_WAIT_OPP;
                } else if (puzzleMode) {
                    if (puzzleMoveCount >= puzzleTarget) {
                        puzzleFailed = true;
                        gameSt = ST_GAMEOVER;
                    } else {
                        gameSt = ST_BOT_THINKING;
                        sf_request_move();
                    }
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
        const char *hdr;
        const char *sub2;
        char puzzleSubBuf[96];

        if (mpMode && (mpOpponentLeft || mpStatus[0] != '\0')) {
            // jocul s-a terminat din cauza deconectarii sau resign
            hdr = "GAME OVER";
            sub2 = mpStatus;
        } else if (puzzleMode) {
            if (puzzleSolved) {
                hdr = "PUZZLE SOLVED";
                snprintf(puzzleSubBuf, sizeof(puzzleSubBuf), "Mate in %d - well done!", puzzleMoveCount);
                sub2 = puzzleSubBuf;
            } else {
                hdr = "PUZZLE FAILED";
                sub2 = mate ? "You got checkmated!" : "No mate within the move limit";
            }
        } else if (gFlagged >= 0) {
            // partida s-a incheiat prin epuizarea timpului
            hdr = "TIME UP";
            int winner = 1 - gFlagged;
            if (mpMode)
                sub2 = (winner == mpMyColor) ? "You win on time!" : "You lose on time";
            else if (botMode)
                sub2 = (winner == 0) ? "You win on time!" : "Bot wins on time";
            else
                sub2 = (winner == 0) ? "White wins on time" : "Black wins on time";
        } else {
            hdr = mate ? "CHECKMATE" : "STALEMATE";
            if (mate) {
                if (botMode)
                    sub2 = (current_turn == 0) ? "Bot wins!" : "You win!";
                else if (mpMode)
                    sub2 = (current_turn == mpMyColor) ? "You lose!" : "You win!";
                else
                    sub2 = (current_turn == 0) ? "Black wins!" : "White wins!";
            } else {
                sub2 = "Draw \xe2\x80\x94 no legal moves";
            }
        }

        Vector2 hv = MeasureTextEx(gFont, hdr, 52, 2);
        Color hdrColor = (puzzleMode && puzzleSolved) ? GOLD : WHITE;
        DrawTextEx(gFont, hdr, (Vector2){ dx + (dw - hv.x) * 0.5f, dy + 22 }, 52, 2, hdrColor);

        Vector2 sv2 = MeasureTextEx(gFont, sub2, 26, 1);
        DrawTextEx(gFont, sub2, (Vector2){ dx + (dw - sv2.x) * 0.5f, dy + 92 }, 26, 1, GOLD);

        if (mpMode) {
            // in multiplayer: butonul duce inapoi la meniul principal
            Rectangle btnPA = { dx + (dw - 200) * 0.5f, dy + dh - 65, 200, 44 };
            if (Btn(btnPA, "Back to Menu", false)) {
                mp_cleanup();
                curScreen = SCR_HOME;
                selRow = selCol = -1;
                hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
                gameSt = ST_SELECT;
            }
        } else if (puzzleMode) {
            // in puzzle: doua butoane - Try Again si Back to Puzzles
            Rectangle btnRetry = { dx + 30, dy + dh - 65, 170, 44 };
            Rectangle btnBack  = { dx + dw - 200, dy + dh - 65, 170, 44 };

            if (Btn(btnRetry, "Try Again", false)) {
                load_puzzle_position(gPuzzles[puzzleIdx].setup, gPuzzles[puzzleIdx].side_to_move);
                puzzleMoveCount = 0;
                puzzleSolved = false;
                puzzleFailed = false;
                selRow = selCol = -1;
                hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
                gameSt = ST_SELECT;
            }

            if (Btn(btnBack, "Next Puzzle", false)) {
                // incarca un alt puzzle aleator (evita repetarea celui curent)
                int newIdx = puzzleIdx;
                if (gPuzzleCount > 1) {
                    while (newIdx == puzzleIdx)
                        newIdx = GetRandomValue(0, gPuzzleCount - 1);
                }
                start_puzzle(newIdx);
            }
        } else {
            Rectangle btnPA = { dx + (dw - 200) * 0.5f, dy + dh - 65, 200, 44 };
            if (Btn(btnPA, "Play Again", false)) {
                init_board();
                current_turn = 0;
                selRow = selCol = -1;
                hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1;
                gameSt = ST_SELECT;
            }
        }
    }

    // proceseaza inputul jucatorului pe tabla (permis doar in starea ST_SELECT)
    if (gameSt == ST_SELECT && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        hintSrcRow = hintSrcCol = hintDstRow = hintDstCol = -1; // sterge vizualizarea hint-ului la orice click

        // previne mutarile facute de om atunci cand este randul botului (negru)
        // in multiplayer, previne mutarile cand nu este randul nostru
        // in puzzle, previne mutarile cand nu este culoarea jucatorului
        if (botMode && current_turn == 1) {
            // masura de precautie: click-urile sunt ignorate in acest moment
        } else if (mpMode && mpMyColor != current_turn) {
            // nu este randul nostru in multiplayer, ignoram click-urile
        } else if (puzzleMode && puzzleUserColor != current_turn) {
            // nu este randul nostru in puzzle (Stockfish raspunde), ignoram
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
                        // salvam coordonatele inainte de a deselecta (pentru trimiterea prin retea)
                        int srcR = selRow, srcC = selCol;

                        // mutare normala
                        execute_move(selRow, selCol, cr, cc, 'Q');  // 'Q' e transmis ca placeholder ignorat

                        // trimite mutarea prin retea daca suntem in modul multiplayer
                        if (mpMode) {
                            char uci[8];
                            coords_to_uci(srcR, srcC, cr, cc, 0, uci);
                            net_send_move(uci);
                        }

                        current_turn = 1 - current_turn; // schimba randul jucatorului
                        selRow = selCol = -1; // deselecteaza

                        // in puzzle mode, incrementam contorul mutarilor jucatorului
                        if (puzzleMode) puzzleMoveCount++;

                        // verifica daca dupa aceasta mutare jocul s-a terminat
                        if (!has_legal_moves(current_turn)) {
                            set_game_over();
                        } else if (mpMode) {
                            // dupa mutare in multiplayer, asteptam mutarea oponentului
                            gameSt = ST_WAIT_OPP;
                        } else if (puzzleMode) {
                            // in puzzle: daca am atins limita de mutari fara mat, esuat
                            if (puzzleMoveCount >= puzzleTarget) {
                                puzzleFailed = true;
                                gameSt = ST_GAMEOVER;
                            } else {
                                gameSt = ST_BOT_THINKING;
                                sf_request_move();
                            }
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

#include "raylib.h"
#include "chess_gui.h"
#include "chess_auth.h"
#include "chess_coach.h"

int main(void)
{
    // setam configuratia ferestrei (antialiasing si suport pentru ecrane de inalta rezolutie)
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WIN_W, WIN_H, "Chess");
    SetTargetFPS(60); // limitam jocul la 60 de cadre pe secunda

    // initializam fonturile folosite pentru desenarea textului si a pieselor
    init_fonts();

    // initializam libcurl si restauram sesiunea salvata (daca exista)
    auth_init();
    auth_load_session();

    // initializam clientul AI Coach (fir worker + curl) — partajeaza libcurl
    // global cu chess_auth si nu apeleaza curl_global_cleanup la cleanup.
    coach_init();

    // bucla principala a jocului
    // ruleaza pana cand utilizatorul inchide fereastra
    while (!WindowShouldClose()) {
        // sincronizam vizibilitatea sidebar-ului coach in functie de ecran
        coach_sync_visibility();

        BeginDrawing();

        // afiseaza ecranul corespunzator starii curente
        if (curScreen == SCR_HOME)
            DrawHome();
        else if (curScreen == SCR_BOTSETUP)
            DrawBotSetup();
        else if (curScreen == SCR_MPSETUP)
            DrawMpSetup();
        else if (curScreen == SCR_MPLOBBY)
            DrawMpLobby();
        else if (curScreen == SCR_PUZZLESETUP)
            DrawPuzzleSetup();
        else if (curScreen == SCR_LOGIN)
            DrawLogin();
        else if (curScreen == SCR_PROFILE)
            DrawProfile();
        else if (curScreen == SCR_TIMESETUP)
            DrawTimeSetup();
        else
            DrawGame();

        EndDrawing();
    }

    // oprim procesul Stockfish (daca a fost pornit) pentru a evita procese ramase in fundal
    sf_stop();
    // oprim procesul bridge de multiplayer (daca exista)
    mp_cleanup();
    // oprim worker-ul AI Coach (asteapta finalizarea unei cereri in curs)
    coach_cleanup();
    // eliberam memoria ocupata de fonturi
    cleanup_fonts();
    // curatam libcurl
    auth_cleanup();
    // inchidem fereastra raylib si eliberam resursele
    CloseWindow();

    return 0;
}

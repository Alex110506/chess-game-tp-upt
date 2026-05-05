#include "raylib.h"
#include "chess_gui.h"

int main(void)
{
    // setam configuratia ferestrei (antialiasing si suport pentru ecrane de inalta rezolutie)
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WIN_W, WIN_H, "Chess");
    SetTargetFPS(60); // limitam jocul la 60 de cadre pe secunda

    // initializam fonturile folosite pentru desenarea textului si a pieselor
    init_fonts();

    // bucla principala a jocului
    // ruleaza pana cand utilizatorul inchide fereastra
    while (!WindowShouldClose()) {
        BeginDrawing();
        
        // afiseaza ecranul corespunzator starii curente
        if (curScreen == SCR_HOME)          
            DrawHome();
        else if (curScreen == SCR_BOTSETUP) 
            DrawBotSetup();
        else                                
            DrawGame();
            
        EndDrawing();
    }

    // oprim procesul Stockfish (daca a fost pornit) pentru a evita procese ramase in fundal
    sf_stop();
    // eliberam memoria ocupata de fonturi
    cleanup_fonts();
    // inchidem fereastra raylib si eliberam resursele
    CloseWindow();
    
    return 0;
}

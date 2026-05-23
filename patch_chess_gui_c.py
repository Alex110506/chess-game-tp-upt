import re

with open("chess_gui.c", "r") as f:
    content = f.read()

# 1. Variables and dispatch rewrite
dispatch_pattern = r'(// Trimite mesajul utilizatorului catre coach folosind starea curenta a partidei\.\nstatic void coach_dispatch_send\(const char \*text\)\n\{\n    if \(\!text || \!\*text\) return;\n    char fen\[160\];\n    board_to_fen\(fen, \(int\)sizeof\(fen\)\);\n    const char \*side  = \(current_turn == 0\) \? "white" : "black";\n    const char \*diff  = \(botDepth <= 1\) \? "easy" : \(botDepth <= 5\) \? "medium" : "hard";\n    // In modul vs bot, jucatorul uman joaca mereu cu piesele albe \(botul cu negrele\)\.\n    // Trimitem explicit culoarea jucatorului ca AI-ul sa nu confunde piesele\.\n    const char \*pcol  = "white";\n    coach_send\(text, fen, gLastMoveUci, side, diff, pcol\);\n\})'

dispatch_repl = '''static bool g_coach_waiting_engine = false;
static char g_coach_pending_text[COACH_INPUT_MAX];

// Trimite mesajul utilizatorului catre coach folosind starea curenta a partidei.
static void coach_dispatch_send(const char *text)
{
    if (!text || !*text) return;
    const char *side  = (current_turn == 0) ? "white" : "black";
    const char *pcol  = "white";

    if (strcmp(side, pcol) == 0) {
        if (sf_pid <= 0) sf_start();
        if (sf_pid > 0) {
            snprintf(g_coach_pending_text, sizeof(g_coach_pending_text), "%s", text);
            g_coach_waiting_engine = true;
            sf_request_move();
        } else {
            // fail safe
            char fen[160];
            board_to_fen(fen, (int)sizeof(fen));
            const char *diff  = (botDepth <= 1) ? "easy" : (botDepth <= 5) ? "medium" : "hard";
            coach_send(text, fen, gLastMoveUci, side, diff, pcol, NULL);
        }
    } else {
        char fen[160];
        board_to_fen(fen, (int)sizeof(fen));
        const char *diff  = (botDepth <= 1) ? "easy" : (botDepth <= 5) ? "medium" : "hard";
        coach_send(text, fen, gLastMoveUci, side, diff, pcol, NULL);
    }
}'''

content = re.sub(dispatch_pattern, dispatch_repl, content)

# 2. Add polling block
poll_pattern = r'(    /\* ── hint: polling pentru bestmove ── \*/\n    if \(gameSt == ST_HINT_THINKING && sf_poll_move\(\)\) \{\n        hintSrcCol = sf_bestmove\[0\] - \'a\';\n        hintSrcRow = 8 - \(sf_bestmove\[1\] - \'0\'\);\n        hintDstCol = sf_bestmove\[2\] - \'a\';\n        hintDstRow = 8 - \(sf_bestmove\[3\] - \'0\'\);\n        gameSt = ST_SELECT;\n    \})'

poll_repl = r'''\1

    /* ── coach: polling pentru bestmove ── */
    if (g_coach_waiting_engine && sf_poll_move()) {
        g_coach_waiting_engine = false;
        char fen[160];
        board_to_fen(fen, (int)sizeof(fen));
        const char *side  = (current_turn == 0) ? "white" : "black";
        const char *diff  = (botDepth <= 1) ? "easy" : (botDepth <= 5) ? "medium" : "hard";
        const char *pcol  = "white";
        coach_send(g_coach_pending_text, fen, gLastMoveUci, side, diff, pcol, sf_bestmove);
    }'''

content = re.sub(poll_pattern, poll_repl, content)

with open("chess_gui.c", "w") as f:
    f.write(content)

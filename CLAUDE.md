# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make chess      # Build terminal game → ./chess
make gui        # Build GUI game → ./chess_gui
make clean      # Remove build artifacts
```

**Dependencies:**
- Terminal build: none (standard C99)
- GUI build: Raylib (via pkg-config or Homebrew fallback on macOS), Stockfish binary in `stockfish/` for bot/hints

## Architecture

Two independent frontends share a single game engine:

### Game Engine — `chess_logic.c / chess_logic.h`
All chess rules and state live here. Both frontends link against this module.

- Board: `char board[8][8]` — uppercase = white pieces, lowercase = black
- Move pipeline: `pseudo_legal()` (piece movement rules) → `try_move_legal()` (simulate to verify king safety) → `execute_move()` (commit state)
- Special moves: castling (tracked via per-rook/king move flags), en passant (target square tracked each turn), pawn promotion
- Game-end detection: `has_legal_moves()` distinguishes checkmate from stalemate; `is_in_check()` for check detection
- `board_to_fen()` converts board state to FEN notation for Stockfish communication

### Terminal Frontend — `chess.c`
Simple game loop. ANSI color board with Unicode piece symbols. Move input format: `e2 e4`.

### GUI Frontend — `chess_gui.c / chess_gui.h` + entry point `gui.c`
Raylib-based, 960×680 window, 75px squares starting at (60, 40).

Two orthogonal state machines:
- **Screen** (`curScreen`): `SCR_HOME` → `SCR_BOTSETUP` → `SCR_GAME`
- **Game state** (`gameSt`): `ST_SELECT` / `ST_PROMOTE` / `ST_GAMEOVER` / `ST_BOT_THINKING` / `ST_BOT_READY` / `ST_HINT_THINKING`

### Stockfish Integration — `chess_gui.c`
Fork/exec with pipe-based IPC; UCI protocol; non-blocking I/O (`O_NONBLOCK`) so the UI stays responsive. Depth per difficulty: Easy=1, Medium=5, Hard=12. Key functions: `sf_start()`, `sf_stop()`, `sf_request_move()`, `sf_poll_move()`, `execute_sf_move()`.

Player statistics (wins / losses / streak) persist to `chess_stats.txt` as a single space-separated line.

## Notes

- Code comments are in Romanian (university project at UPT).
- The Stockfish binary is excluded from git (`.gitignore`) due to size; it must be present in `stockfish/` for bot features to work.
- No dynamic memory allocation except for font loading in the GUI.

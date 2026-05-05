# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Terminal-based game
make chess

# GUI game (requires Raylib)
make gui

# Clean build artifacts
make clean
```

The build system uses `pkg-config` to find Raylib and falls back to Homebrew paths (`/opt/homebrew`) on macOS. The GUI target links against macOS frameworks (OpenGL, Cocoa, IOKit, CoreAudio, CoreVideo).

## Architecture

Two completely independent frontends share one backend:

- **chess_logic.c / chess_logic.h** — pure game engine, no I/O. All chess rules, move validation, board state. The only module both UIs depend on.
- **chess.c** — terminal UI (ANSI colors, Unicode pieces, Romanian-language prompts)
- **chess_gui.c / chess_gui.h + gui.c** — Raylib GUI. `gui.c` is the entry point; `chess_gui.c` contains all rendering and state management.

### Board Representation

8×8 `char board[8][8]` with uppercase = white, lowercase = black, `'.'` = empty. Pieces: K/Q/R/B/N/P (king/queen/rook/bishop/knight/pawn).

### Move Validation (two-stage)

1. `pseudo_legal()` — checks piece movement geometry only
2. `try_move_legal()` — simulates the move and calls `is_sqare_attacked()` to verify the king is not left in check

### Special Moves

- **Castling**: validated via static flags in `chess_logic.c` tracking whether king/rooks have moved
- **En passant**: target square stored after double pawn push; captured pawn removed from its rank
- **Promotion**: GUI shows piece-selection overlay; terminal prompts user; bot always promotes to queen

### Stockfish Integration (GUI only)

Engine runs as a child process via `fork()` + pipes. Communication uses UCI protocol (`position fen ...`, `go depth N`, reads `bestmove`). Reads are non-blocking (`O_NONBLOCK`) to keep the UI responsive. Depth: 1 (Easy), 5 (Medium), 12 (Hard).

### GUI State Machine

Screen states: `SCR_HOME` → `SCR_BOTSETUP` → `SCR_GAME`

Game states within `SCR_GAME`: `ST_SELECT`, `ST_PROMOTE`, `ST_GAMEOVER`, `ST_BOT_THINKING`, `ST_BOT_READY`, `ST_HINT_THINKING`

Player stats (wins/losses/streak) are persisted to `chess_stats.txt` as three space-separated integers.

# Chess Game

A fully-featured chess game written in C, developed as a university project at UPT (Universitatea Politehnica Timișoara). The project ships two complete interfaces - a terminal client and a graphical GUI - both powered by the same shared game engine.

## Features

### Game Modes
- **Local 1v1** - two players on the same machine
- **Play vs Bot** - face off against a Stockfish-powered AI at three difficulty levels: Easy, Medium, and Hard

### Chess Rules
- Full legal move validation for all piece types
- **Castling** (kingside and queenside)
- **En passant**
- **Pawn promotion** - choose your piece in the GUI; terminal prompts for input
- **Check and checkmate detection**
- **Stalemate detection**

### Graphical Interface (GUI)
- Built with [Raylib](https://www.raylib.com/)
- Mouse-driven piece selection and movement
- Legal move indicators - available squares highlighted on selection
- Check highlighted in red
- Pawn promotion overlay with visual piece picker
- **Hint system** - request a suggested move powered by Stockfish
- Player statistics tracked across sessions (wins, losses, winning streak)
- Home menu and bot difficulty setup screen

### Terminal Interface
- Colored Unicode chess pieces via ANSI escape codes
- Coordinate-based move input (e.g. `e2 e4`)
- Runs in any standard terminal

### AI - Stockfish Integration
- Stockfish engine communicates over UCI protocol via a child process and pipes
- Non-blocking I/O keeps the GUI responsive while the engine thinks
- Difficulty maps to search depth: Easy = 1, Medium = 5, Hard = 12

## Dependencies

| Interface | Dependency |
|-----------|-----------|
| Terminal  | None (standard C99) |
| GUI       | [Raylib](https://www.raylib.com/), Stockfish (optional, for bot/hints) |

Install Raylib on macOS:
```bash
brew install raylib
```

Install Stockfish on macOS:
```bash
brew install stockfish
```

## Building

```bash
# Terminal game
make chess

# GUI game
make gui

# Remove build artifacts
make clean
```

## Running

```bash
# Terminal
./chess

# GUI
./chess_gui
```

## Project Structure

```
chess.c          # Terminal UI entry point and game loop
chess_logic.c    # Shared game engine (rules, validation, FEN)
chess_logic.h    # Public interface for the game engine
gui.c            # GUI entry point
chess_gui.c      # GUI rendering, state machine, Stockfish integration
chess_gui.h      # GUI constants and declarations
makefile
```

## University Context

This project was developed as a practical assignment for a programming course at **Universitatea Politehnica Timișoara (UPT)**. It demonstrates modular C design, separation of logic from presentation, inter-process communication, and graphical application development.

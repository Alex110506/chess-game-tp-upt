# Chess Game

A fully-featured chess game written in C, developed as a university project at UPT (Universitatea Politehnica Timișoara). The project ships two complete interfaces — a terminal client and a graphical GUI — both powered by the same shared game engine. The GUI supports local play, bot matches, online multiplayer with ELO ranking, and checkmate puzzles.

## Features

### Game Modes
- **Local 1v1** — two players on the same machine with optional chess clock (1, 5, or 10 minutes)
- **Play vs Bot** — face off against a Stockfish-powered AI at three difficulty levels: Easy (depth 1), Medium (depth 5), and Hard (depth 12)
- **Online Multiplayer** — host or join a game room over the internet via WebSocket relay; supports optional timed games and ELO ranking
- **Puzzles** — solve "checkmate in N" puzzles with varying difficulty (Easy / Medium / Hard); Stockfish defends the opposing side; a random puzzle is loaded each time

### Chess Rules
- Full legal move validation for all piece types
- **Castling** (kingside and queenside)
- **En passant**
- **Pawn promotion** — choose your piece in the GUI; terminal prompts for input
- **Check and checkmate detection**
- **Stalemate detection**

### Graphical Interface (GUI)
- Built with [Raylib](https://www.raylib.com/)
- Mouse-driven piece selection and movement
- Legal move indicators — available squares highlighted on selection
- Check highlighted in red
- Pawn promotion overlay with visual piece picker
- **Hint system** — request a suggested move powered by Stockfish
- **Board inversion** — the board flips automatically when playing as Black (multiplayer and puzzles)
- **Chess clock** — optional countdown timer for Local 1v1 and Multiplayer games; flag detection when time runs out
- **Puzzle panel** — shows puzzle name, difficulty badge, and move counter during puzzle mode
- Player statistics tracked across sessions (wins, losses, winning streak)
- Home screen with 2×2 game mode grid, decorative pieces, and stat bar

### Terminal Interface
- Colored Unicode chess pieces via ANSI escape codes
- Coordinate-based move input (e.g. `e2 e4`)
- Runs in any standard terminal

### AI — Stockfish Integration
- Stockfish engine communicates over UCI protocol via a child process and pipes
- Non-blocking I/O keeps the GUI responsive while the engine thinks
- Used for bot mode, the hint button, and as the defender in puzzle mode

### Online Multiplayer & Accounts
- **Room-based matchmaking** — one player hosts (receives a 4-character room code), the other joins
- Real-time move relay via a FastAPI + WebSocket server
- **User accounts** — register and log in to track stats across sessions
- **ELO ranking** — wins and losses update both players' rankings (default start: 1200)
- **Leaderboard** endpoint (top 10 by rank)
- **Session persistence** — token is saved locally so you stay logged in between launches
- Play as guest (unranked) or sign in before entering multiplayer
- Resign support and opponent disconnect detection

### Puzzle Mode
- 9 handcrafted checkmate puzzles (mate in 1 and mate in 2)
- Three difficulty tiers: Easy, Medium, Hard
- Stockfish plays the defender at high depth (14) for optimal resistance
- Success / failure feedback with option to skip or try the next puzzle

## Dependencies

| Component | Dependency |
|-----------|------------|
| Terminal game | None (standard C99) |
| GUI game | [Raylib](https://www.raylib.com/), [libcurl](https://curl.se/libcurl/) (for auth HTTP), Stockfish (optional, for bot/hints/puzzles) |
| Server | Python 3.10+, FastAPI, Uvicorn, Motor (async MongoDB driver), MongoDB |

### Install — macOS (Homebrew)

```bash
brew install raylib
brew install stockfish
brew install curl   # usually pre-installed
```

### Install — Server (Python)

```bash
cd server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Building

```bash
# Terminal game
make chess

# GUI game (links Raylib + libcurl)
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

### Running the Multiplayer Server

```bash
cd server
source .venv/bin/activate
export MONGODB_URI="mongodb+srv://<user>:<pass>@<cluster>/chess?retryWrites=true&w=majority"
uvicorn app:app --host 0.0.0.0 --port 8765
```

The server URL defaults to `ws://127.0.0.1:8765/ws`. Set the `CHESS_BACKEND_URL` environment variable to point the client at a different backend.

## Project Structure

```
chess.c             # Terminal UI entry point and game loop
chess_logic.c/.h    # Shared game engine (rules, validation, FEN, puzzle loader)
gui.c               # GUI entry point (main loop + screen dispatch)
chess_gui.c/.h      # GUI rendering, state machine, Stockfish integration, puzzles
chess_net.c/.h      # Networking module (fork/pipe bridge to Python WS client)
chess_auth.c/.h     # Authentication module (libcurl HTTP to FastAPI backend)
chess_stats.txt     # Local player statistics (wins, losses, streak)
chess_session.txt   # Persisted auth session (token + username)
makefile            # Build recipes for terminal and GUI targets

server/
├── app.py              # FastAPI backend (WebSocket relay + REST auth + ELO)
├── net_client.py       # Python WebSocket bridge (stdin/stdout ↔ WS)
├── requirements.txt    # Python dependencies
├── run_server.sh       # Convenience script to start the server
└── .env.example        # Environment variable template

stockfish/          # Stockfish engine binary (excluded from git)
```

## University Context

This project was developed as a practical assignment for a programming course at **Universitatea Politehnica Timișoara (UPT)**. It demonstrates modular C design, separation of logic from presentation, inter-process communication (Stockfish + network bridge), client-server architecture with WebSockets, and graphical application development with Raylib.

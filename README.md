# Chess

A fully-featured chess platform built in C and Python. Ships two complete clients — a Raylib graphical GUI and a terminal client — backed by a FastAPI server with user accounts, ELO ranking, and Stripe-powered AI Coach Pro subscriptions. All frontends share a single game engine.

---

## Features

### Game Modes (GUI & Terminal)
| Mode | Description |
|---|---|
| **Local 1v1** | Two players on the same machine with an optional chess clock (1, 5, or 10 minutes) |
| **Play vs Bot** | Stockfish-powered AI at three difficulty levels: Easy (depth 1), Medium (depth 5), Hard (depth 12) |
| **Online Multiplayer** | Host or join a room via a 4-character code; supports optional timed games and ELO ranking |
| **Puzzles** | 9 handcrafted "checkmate in N" puzzles (Easy / Medium / Hard); Stockfish defends at depth 14 |

### Chess Rules
- Full legal move validation for all piece types
- Castling (kingside and queenside), en passant, pawn promotion
- Check, checkmate, and stalemate detection

### Graphical GUI (Raylib)
- Mouse-driven piece selection with legal-move highlights
- Check highlighted in red; promotion overlay with visual piece picker
- **Hint system** — Stockfish-powered move suggestion on demand
- **Chess clock** — countdown timer with automatic flag detection
- **Board inversion** — flips automatically when playing as Black
- **Puzzle panel** — name, difficulty badge, and move counter
- **Profile screen** — ELO rank, W/L/T stats, and subscription status card
- **AI Coach Pro badge** on the home-screen profile bar when subscribed
- **Manage Subscription** button opens the web app account page in the browser
- Session persistence across launches (token saved locally)

### Terminal Client
- ANSI-colored Unicode chess pieces
- Coordinate-based move input (`e2 e4`)
- Runs in any standard terminal

### Online Multiplayer & Accounts
- Room-based matchmaking — host gets a 4-character code, guest joins
- Real-time move relay over WebSocket (FastAPI backend)
- **User accounts** — register, log in, persist stats and rank across sessions
- **ELO ranking** — both players' ratings updated on every ranked game (K=32, default 1200)
- **Global leaderboard** — top 10 players by ELO
- Play as guest (unranked) or sign in before entering a room
- Resign support and opponent-disconnect detection

### AI Coach Pro (Subscription)
- **$4.99 / month** via Stripe hosted checkout
- Post-game move analysis, tactical missed-opportunity alerts, opening coach
- Subscription managed through the web app account page
- Status (`free` / `pro` / `cancelling`) reflected in both the GUI profile and the web app
- Cancellation sets `cancel_at_period_end` — access continues until the billing period ends
- Stripe webhook handles provisioning and de-provisioning automatically

### Web App
- React 19 + TypeScript SPA served by Vite
- Glassmorphism dark theme (navy / forest-green / gold)
- Pages: Home (hero, feature grid, live leaderboard), Login, Register, Account
- Account page: player stats, subscription card, Stripe checkout and cancellation

---

## Tech Stack

| Layer | Technology |
|---|---|
| Game engine | C99 (shared by both frontends) |
| GUI client | [Raylib](https://www.raylib.com/), libcurl |
| Terminal client | C99, standard library only |
| Backend server | Python 3.10+, FastAPI, Uvicorn, Motor (async MongoDB driver) |
| Database | MongoDB Atlas |
| Payments | Stripe (hosted checkout, webhooks) |
| Web frontend | React 19, TypeScript, Vite, React Router 7 |
| Stockfish | UCI protocol over fork/pipe IPC |

---

## Project Structure

```
chess.c             # Terminal client entry point
chess_logic.c/.h    # Shared game engine — rules, validation, FEN, puzzle loader
gui.c               # GUI entry point (main loop, screen dispatch)
chess_gui.c/.h      # GUI rendering, state machines, Stockfish IPC, puzzles
chess_net.c/.h      # Networking (fork/pipe bridge to Python WebSocket client)
chess_auth.c/.h     # Auth module — libcurl HTTP client for FastAPI endpoints
makefile            # Build recipes

server/
├── app.py              # FastAPI: WebSocket relay, REST auth, ELO, Stripe billing
├── net_client.py       # Python WebSocket bridge (stdin/stdout ↔ WS)
├── requirements.txt    # Python dependencies
└── .env.example        # Environment variable reference

web/
├── src/
│   ├── pages/          # Home, Login, Register, Account
│   ├── components/     # Navbar, Footer
│   └── lib/api.ts      # Typed API client (Bearer token auth)
├── index.html
└── vite.config.ts

stockfish/          # Stockfish binary — excluded from git (add manually)
```

---

## Dependencies

### GUI client — macOS (Homebrew)
```bash
brew install raylib curl
# Copy the Stockfish binary into stockfish/ for bot, hints, and puzzles
```

### Server
```bash
cd server
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

### Web frontend
```bash
cd web
npm install
```

---

## Building the C clients

```bash
make chess     # terminal client → ./chess
make gui       # GUI client      → ./chess_gui
make clean     # remove build artifacts
```

---

## Running

### GUI
```bash
./chess_gui
```

### Terminal
```bash
./chess
```

### Backend server
```bash
cd server
cp .env.example .env   # fill in your values
source .venv/bin/activate
uvicorn app:app --host 0.0.0.0 --port 8765
```

### Web frontend
```bash
cd web
npm run dev    # http://localhost:5173
```

### Stripe webhooks (local development)
```bash
stripe listen --forward-to http://localhost:8765/stripe/webhook
# Copy the printed whsec_… into server/.env as STRIPE_WEBHOOK_SECRET
```

---

## Environment Variables

All server configuration lives in `server/.env` (see `server/.env.example`):

| Variable | Description |
|---|---|
| `MONGODB_URI` | MongoDB Atlas connection string |
| `MONGODB_DB` | Database name (default: `chess`) |
| `STRIPE_SECRET_KEY` | Stripe secret key (`sk_test_…` or `sk_live_…`) |
| `STRIPE_PRICE_ID` | Price ID for the $4.99/month subscription (`price_…`) |
| `STRIPE_WEBHOOK_SECRET` | Stripe webhook signing secret (`whsec_…`) |
| `FRONTEND_URL` | Web frontend URL for Stripe redirect (default: `http://localhost:5173`) |

GUI client environment variables:

| Variable | Description |
|---|---|
| `CHESS_BACKEND_URL` | Backend URL (default: `http://127.0.0.1:8765`) |
| `CHESS_FRONTEND_URL` | Web app URL for the "Manage Subscription" button (default: `http://localhost:5173`) |

---

## API Reference

| Method | Endpoint | Auth | Description |
|---|---|---|---|
| POST | `/auth/register` | — | Create account |
| POST | `/auth/login` | — | Login, returns Bearer token |
| POST | `/auth/logout` | ✓ | Invalidate token |
| GET | `/me` | ✓ | Get profile (username, rank, stats, subscription) |
| POST | `/game/report` | ✓ | Report game result, update ELO |
| GET | `/leaderboard` | — | Top 10 players by ELO |
| POST | `/stripe/create-checkout-session` | ✓ | Create Stripe checkout session |
| POST | `/stripe/cancel-subscription` | ✓ | Cancel subscription at period end |
| POST | `/stripe/webhook` | Stripe sig | Handle Stripe events |
| WS | `/ws` | — | Multiplayer relay |

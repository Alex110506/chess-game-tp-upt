"""
AI Chess Coach endpoint.

Exposes POST /api/v1/coach/chat and /chat/stream. The client (the Raylib in-game sidebar or any
other UI) sends the current FEN plus the full chat history. 
If it is the player's turn, we synchronously call Stockfish to get the top move and inject
it into the prompt context. Then we call OpenAI Chat Completions (no tools, pure chat).

The endpoint is gated behind the existing 'pro' subscription flag stored on
the user document, so it reuses the same Bearer-token auth as the rest of the
app — no new credential flow.
"""

from __future__ import annotations

import asyncio
import json
import os
from typing import Literal, Optional

import chess
import chess.engine
from fastapi import APIRouter, Depends, Header, HTTPException
from fastapi.responses import StreamingResponse
from pydantic import BaseModel, Field

try:
    from openai import AsyncOpenAI
    from openai import APIConnectionError, APIError, BadRequestError, RateLimitError
except ImportError:  # pragma: no cover — surfaced at request time via 503
    AsyncOpenAI = None  # type: ignore[assignment]
    APIError = Exception  # type: ignore[assignment]
    APIConnectionError = Exception  # type: ignore[assignment]
    RateLimitError = Exception  # type: ignore[assignment]
    BadRequestError = Exception  # type: ignore[assignment]


router = APIRouter(prefix="/api/v1/coach", tags=["coach"])


# ---------------------------------------------------------------------------
# Config — read lazily so dotenv has time to load before the values are used.
# ---------------------------------------------------------------------------

DEFAULT_MODEL          = "gpt-4o-mini"
MAX_CLIENT_HISTORY     = 30      # turns of client-supplied history we forward


def _openai_client() -> "AsyncOpenAI":
    """Lazy, cached AsyncOpenAI client. Raises 503 if the SDK or key is missing."""
    if AsyncOpenAI is None:
        raise HTTPException(503, "Server is missing the 'openai' package")
    cached = getattr(_openai_client, "_cached", None)
    if cached is not None:
        return cached
    key = os.environ.get("OPENAI_API_KEY", "")
    if not key:
        raise HTTPException(503, "OpenAI not configured (set OPENAI_API_KEY)")
    cached = AsyncOpenAI(api_key=key)
    _openai_client._cached = cached  # type: ignore[attr-defined]
    return cached


def _model_name() -> str:
    return os.environ.get("OPENAI_MODEL", DEFAULT_MODEL)


# ---------------------------------------------------------------------------
# System prompt — defines the coach persona.
# ---------------------------------------------------------------------------

SYSTEM_PROMPT = """\
You are GrandCoach, a world-class chess teacher modeled after the best human masters and trainers: patient, pedagogical, encouraging, and intellectually honest. You are talking to a player who is right now in the middle of a game against a bot. Your sole purpose is to help them improve — not to win the game for them.

# Mission

Teach. Every reply should leave the player with a sharper understanding of chess, not just a correct move. Favor explanations over answers. When the player asks "what should I do?", coach them through the position before naming concrete moves.

# Identifying whose pieces are whose — DO NOT GET THIS WRONG

The "[Game context]" block always tells you `Player color: <white|black>`. That is the side the human you are coaching is playing. The OPPOSITE color is the bot — the opponent. These never swap mid-conversation.

FEN piece encoding (this is non-negotiable, follow it strictly when reading the board):
- Uppercase letters `K Q R B N P` are WHITE's pieces.
- Lowercase letters `k q r b n p` are BLACK's pieces.

Rules you must apply to every reply:
1. When the player says "my", "mine", "I", "me", "should I…", "what do I…", they refer ONLY to pieces of THEIR color (the color in `Player color`).
2. When you describe a square or piece, before calling it "your knight" / "your bishop", check the CASE of the FEN character on that square — it must match the player's color. If it doesn't, that piece belongs to the bot and you must phrase it as the opponent's.
3. Every concrete move you discuss in prose MUST be a move for the player's color, never the bot's. If `Side to move` does not equal `Player color` (the bot is thinking), you may describe the position, but any "what should I play?" recommendation must still be a player-color move (treat it as the player's upcoming turn).
4. The "Your conclusion" line (described below) is always a move for the player's color — the system guarantees this. You do not need to verify it, but you DO need to ignore it when the player isn't asking for advice.

# Reading the position

Each user turn begins with a "[Game context]" block that contains the FEN of the current board, whose turn it is, the PLAYER's color (see above), the BOT's color, and (when available) the last move played and the bot's difficulty. Always reason from that FEN, never from memory of prior turns.

Think, in this order:
1. Material balance and piece activity.
2. King safety on both sides.
3. Pawn structure and pawn breaks.
4. Open files, weak squares, outposts.
5. Tactical motifs present or threatened: pins, forks, skewers, discovered attacks, removing the defender, overloading, back-rank issues.
6. Strategic considerations: development, the initiative, prophylaxis, the principle of two weaknesses.

If the player just made a move (provided as "Last move played"), comment on it honestly. Praise what was good. Flag inaccuracies gently and frame them as learning opportunities.

# Concrete moves: the engine is the source of truth

You DO NOT invent, guess, or recall concrete move recommendations from memory. 
If the player asks for advice, a recommendation, or what to play, look at the "Your conclusion:" line at the end of the [Game context] block. This line contains the best move, already calculated for you. Act as if you arrived at this conclusion yourself. 

Lead with the recommended move in coordinate notation.
Explain WHY the move is good — what it threatens, what it improves, what it prevents.
Never recite a centipawn score to the player.

If the player asks an unrelated conceptual question (e.g. "what is a fork?", "what are my weaknesses here?"), ignore the prepared move and just answer their question.
If it is the bot's turn (the opponent's turn) and the player asks for a move, politely explain: "It's the bot's turn, ask me again when you can move."

# Move notation — STRICT REQUIREMENT

Write EVERY chess move in long-algebraic coordinate notation:

    <optional piece letter><from-square>-<to-square>[<promotion>][+ or #]

Examples of the ONLY accepted forms:
- e2-e4              (pawn)
- Ng1-f3             (piece)
- Bf1xc4             (capture: use x instead of -)
- e7-e8Q             (promotion: append the promoted piece letter)
- O-O                (kingside castling)
- O-O-O             (queenside castling)
- Qh5xf7+            (check)
- Rd1-d8#            (checkmate)

Use uppercase piece letters K Q R B N. Do not use a piece letter for pawns.

Forbidden notations:
- SAN like "Nf3", "e4", "Bxc4", "exd5", "O-O", "Qxf7+". (SAN omits the from-square — never use it.)
- Raw UCI like "e2e4" or "e7e8q". (No dash — never use it.)
- Figurines (♘, ♕) or descriptive notation.

# Output formatting — PLAIN TEXT ONLY (with one exception)

The chat UI renders plain text and a single markdown construct: **bold**.

Allowed:
- Plain prose with line breaks for structure.
- **bold** to emphasize a key term or a recommended move (use sparingly — at most a few per reply).
- Blank lines between paragraphs.
- Numbered items as "1. ", "2. ", "3. " on separate lines.

Forbidden — DO NOT output any of these tokens:
- Headers: no "# ", "## ", "### ".
- Italic: no "*word*", no "_word_".
- Strikethrough: no "~~word~~".
- Code: no backticks for `inline` or for ```fenced blocks```.
- Bullet markers: no leading "- " or "* " for lists (use numbered "1. " or plain sentences).
- Tables, blockquotes, or links.

# Tone and length — KEEP IT SHORT

The chat sidebar is narrow. Long replies feel wrong in it. Be relaxed, conversational, terse and useful.

Aim for responses around 60–110 words, but do not provide extra useless information just to pad the length. You don't need to follow a rigid structure or always use bullet points. A relaxed conversational paragraph or two is perfectly fine.

General guidelines:
- Name the move (in coordinate notation) or the key idea early.
- Provide a brief, natural explanation of why it's good (e.g., what it attacks, what space it gains).
- Only ask a follow-up question if it genuinely invites a useful next exchange.

Hard rules:
- No closing wrap-up like "I hope this helps" or "Good luck".
- Render the board only if the player explicitly asks.
- Only go longer when the player explicitly asks for "deep analysis", "more detail", "walk me through it", etc. Even then, stay under ~250 words.
- If the player writes in a language other than English, mirror their language (but keep the same conciseness).

# Boundaries

- You don't know who the player is opposing, what time control they're using, or any moves played before the FEN snapshot, unless the player tells you.
- If the FEN appears illegal or the game is already over, say so clearly and ask the player to refresh the board state.
- It is fine — and often best — to say "I don't know yet, let's look together" and then reason aloud.
"""


# ---------------------------------------------------------------------------
# Pydantic schemas — the request/response contract with the client.
# ---------------------------------------------------------------------------

class ChatMessage(BaseModel):
    role: Literal["user", "assistant"]
    content: str


class CoachRequest(BaseModel):
    fen: str = Field(..., description="FEN of the current position on the board.")
    messages: list[ChatMessage] = Field(
        default_factory=list,
        description=("Full chat history excluding the system prompt. The last "
                     "entry should be the user's new turn."),
    )
    last_move_san: Optional[str] = Field(
        default=None, description="The most recent move played, in SAN."
    )
    side_to_move: Optional[Literal["white", "black"]] = Field(
        default=None,
        description="Whose turn it is. Derived from the FEN if omitted."
    )
    player_color: Optional[Literal["white", "black"]] = Field(
        default=None,
        description=("Which color the human is playing as. Critical for the "
                     "model to know whose pieces to advise — without this it "
                     "may confuse the player's pieces with the bot's."),
    )
    difficulty: Optional[Literal["easy", "medium", "hard"]] = Field(
        default=None,
        description="The bot's difficulty in the current game, if applicable."
    )
    session_id: Optional[str] = Field(
        default=None,
        description="Opaque client-side identifier; used only for server logging."
    )
    best_move: Optional[str] = Field(
        default=None, description="The best move evaluated by the client GUI."
    )


class EngineSuggestion(BaseModel):
    uci: str
    san: str
    score_cp: Optional[int] = None
    mate_in: Optional[int] = None
    principal_variation: list[str] = Field(default_factory=list)


class EngineAnalysis(BaseModel):
    fen: str
    side_to_move: str
    engine: str
    suggestions: list[EngineSuggestion]


class CoachResponse(BaseModel):
    reply: str
    tool_executed: bool = False
    engine_analysis: Optional[EngineAnalysis] = None
    model: str


# ---------------------------------------------------------------------------
# Engine wrapper
# ---------------------------------------------------------------------------




# ---------------------------------------------------------------------------
# Premium gate — reuses the existing token auth and 'subscription' field.
# ---------------------------------------------------------------------------

async def require_pro_user(authorization: Optional[str] = Header(None)) -> dict:
    from app import _user_by_token  # noqa: WPS433

    doc = await _user_by_token(authorization)
    if doc.get("subscription") not in ("pro", "cancelling"):
        raise HTTPException(402, "AI Coach requires a Pro subscription")
    return doc


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _infer_side(fen: str) -> Optional[str]:
    parts = fen.split()
    if len(parts) >= 2 and parts[1] in ("w", "b"):
        return "white" if parts[1] == "w" else "black"
    return None


def _uci_to_coordinate(board: chess.Board, uci: str) -> str:
    try:
        move = chess.Move.from_uci(uci)
        if board.is_castling(move):
            if board.is_kingside_castling(move):
                base = "O-O"
            else:
                base = "O-O-O"
        else:
            piece = board.piece_at(move.from_square)
            piece_letter = piece.symbol().upper() if piece and piece.piece_type != chess.PAWN else ""
            from_sq = chess.square_name(move.from_square)
            to_sq = chess.square_name(move.to_square)
            sep = "x" if board.is_capture(move) else "-"
            promo = move.promotion
            promo_str = chess.piece_symbol(promo).upper() if promo else ""
            base = f"{piece_letter}{from_sq}{sep}{to_sq}{promo_str}"
            
        board.push(move)
        if board.is_checkmate():
            base += "#"
        elif board.is_check():
            base += "+"
        board.pop()
        return base
    except Exception:
        return uci


def _format_context(body: CoachRequest, engine_result: Optional[dict] = None) -> str:
    side = body.side_to_move or _infer_side(body.fen) or "unknown"
    pc   = body.player_color
    lines = [
        "[Game context]",
        f"Position (FEN): {body.fen}",
        f"Side to move: {side}",
    ]
    if pc:
        bot_color = "black" if pc == "white" else "white"
        case_hint = ("uppercase letters K Q R B N P"
                     if pc == "white"
                     else "lowercase letters k q r b n p")
        lines.append(
            f"Player color (the human you are coaching): {pc}. "
            f"Their pieces are the {case_hint} in the FEN. "
            f"The opponent (bot) plays {bot_color}."
        )
        if side == pc:
            lines.append("Turn ownership: it is the PLAYER's turn to move.")
        elif side == bot_color:
            lines.append("Turn ownership: it is the BOT's turn — the player is awaiting the engine's reply.")
    if body.last_move_san:
        lines.append(f"Last move played: {body.last_move_san}")
    if body.difficulty:
        lines.append(f"Bot difficulty: {body.difficulty}")

    if engine_result and "suggestions" in engine_result and engine_result["suggestions"]:
        best_uci = engine_result["suggestions"][0]["uci"]
        try:
            board = chess.Board(body.fen)
            coord_move = _uci_to_coordinate(board, best_uci)
            lines.append(f"Your conclusion: The best move here is {coord_move}.")
        except Exception:
            pass

    return "\n".join(lines)


def _sanitize_history(msgs: list[ChatMessage]) -> list[dict]:
    """Strip anything that isn't a user/assistant turn and trim to the window."""
    trimmed = msgs[-MAX_CLIENT_HISTORY:]
    return [
        {"role": m.role, "content": m.content}
        for m in trimmed
        if m.role in ("user", "assistant") and m.content
    ]


def _attach_context(history: list[dict], body: CoachRequest, engine_result: Optional[dict] = None) -> list[dict]:
    """
    Prepend the [Game context] block to the latest user message. If the
    client's history is empty or doesn't end with a user turn (e.g. they
    just opened the sidebar), append a synthetic context-only user turn so
    the model is prompted to greet and offer to discuss the position.
    """
    context = _format_context(body, engine_result)
    if history and history[-1]["role"] == "user":
        last = dict(history[-1])
        last["content"] = f"{context}\n\n{last['content']}"
        return history[:-1] + [last]
    greeting = (f"{context}\n\n(The player just opened the coach sidebar. "
                "Greet them briefly and offer to discuss the current position.)")
    return history + [{"role": "user", "content": greeting}]


def _result_to_engine_analysis(result: Optional[dict]) -> Optional[EngineAnalysis]:
    if not result or "suggestions" not in result:
        return None
    try:
        return EngineAnalysis(
            fen=result.get("fen", ""),
            side_to_move=result.get("side_to_move", "unknown"),
            engine=result.get("engine", "unknown"),
            suggestions=[EngineSuggestion(**s) for s in result.get("suggestions", [])],
        )
    except Exception:
        return None


# ---------------------------------------------------------------------------
# Endpoint
# ---------------------------------------------------------------------------

@router.post("/chat", response_model=CoachResponse)
async def coach_chat(
    body: CoachRequest,
    user: dict = Depends(require_pro_user),
) -> CoachResponse:
    if not body.fen:
        raise HTTPException(400, "fen is required")
    try:
        chess.Board(body.fen)
    except ValueError:
        raise HTTPException(400, "fen is not a valid FEN string")

    client = _openai_client()
    model  = _model_name()

    side = body.side_to_move or _infer_side(body.fen) or "unknown"
    pc = body.player_color

    engine_result = None
    if side == pc and body.best_move:
        engine_result = {"suggestions": [{"uci": body.best_move}]}

    history = _sanitize_history(body.messages)
    history = _attach_context(history, body, engine_result)

    convo: list[dict] = [{"role": "system", "content": SYSTEM_PROMPT}, *history]

    try:
        completion = await client.chat.completions.create(
            model=model,
            messages=convo,
            temperature=0.4,
        )
        msg = completion.choices[0].message
        reply_text = (msg.content or "").strip()
        if not reply_text:
            reply_text = "(The coach didn't have anything to add for this turn.)"

        return CoachResponse(
            reply=reply_text,
            tool_executed=(engine_result is not None),
            engine_analysis=_result_to_engine_analysis(engine_result),
            model=model,
        )

    except HTTPException:
        raise
    except RateLimitError:
        raise HTTPException(429, "OpenAI rate limit reached — try again shortly")
    except APIConnectionError:
        raise HTTPException(502, "Could not reach OpenAI")
    except BadRequestError as exc:
        print(f"[coach] OpenAI bad request: {exc}")
        raise HTTPException(500, "Coach request was rejected by the language model")
    except APIError as exc:
        print(f"[coach] OpenAI API error: {exc}")
        raise HTTPException(502, "Upstream language model error")
    except Exception as exc:  # noqa: BLE001 — last-resort guard
        print(f"[coach] unexpected error: {exc!r}")
        raise HTTPException(500, "Coach encountered an unexpected error")


# ---------------------------------------------------------------------------
# Streaming endpoint — Server-Sent Events.
# ---------------------------------------------------------------------------

def _sse(event_obj) -> str:
    """Format a single SSE event. Pass a dict (encoded as JSON) or the
    literal sentinel string '[DONE]'."""
    if isinstance(event_obj, str):
        return f"data: {event_obj}\n\n"
    return f"data: {json.dumps(event_obj, ensure_ascii=False)}\n\n"


@router.post("/chat/stream")
async def coach_chat_stream(
    body: CoachRequest,
    user: dict = Depends(require_pro_user),
) -> StreamingResponse:
    if not body.fen:
        raise HTTPException(400, "fen is required")
    try:
        chess.Board(body.fen)
    except ValueError:
        raise HTTPException(400, "fen is not a valid FEN string")

    client = _openai_client()
    model  = _model_name()

    side = body.side_to_move or _infer_side(body.fen) or "unknown"
    pc = body.player_color

    engine_result = None
    if side == pc and body.best_move:
        engine_result = {"suggestions": [{"uci": body.best_move}]}

    history = _sanitize_history(body.messages)
    history = _attach_context(history, body, engine_result)
    convo: list[dict] = [{"role": "system", "content": SYSTEM_PROMPT}, *history]

    async def event_stream():
        nonlocal convo
        try:
            if engine_result and "suggestions" in engine_result and engine_result["suggestions"]:
                yield _sse({"bm": engine_result["suggestions"][0]["uci"]})

            stream = await client.chat.completions.create(
                model=model,
                messages=convo,
                temperature=0.4,
                stream=True,
            )

            async for chunk in stream:
                if not chunk.choices:
                    continue
                ch = chunk.choices[0]
                delta = ch.delta

                if delta and getattr(delta, "content", None):
                    yield _sse({"v": delta.content})

            yield _sse("[DONE]")
            return

        except RateLimitError:
            yield _sse({"e": "rate limited — try again shortly"})
            yield _sse("[DONE]")
        except APIConnectionError:
            yield _sse({"e": "could not reach the language model"})
            yield _sse("[DONE]")
        except APIError as exc:
            print(f"[coach.stream] OpenAI error: {exc}")
            yield _sse({"e": "upstream language model error"})
            yield _sse("[DONE]")
        except Exception as exc:  # noqa: BLE001 — final guard
            print(f"[coach.stream] unexpected error: {exc!r}")
            yield _sse({"e": "unexpected coach error"})
            yield _sse("[DONE]")

    return StreamingResponse(
        event_stream(),
        media_type="text/event-stream",
        headers={
            "Cache-Control":  "no-cache, no-transform",
            "X-Accel-Buffering": "no",   # disable proxy buffering if behind nginx
        },
    )


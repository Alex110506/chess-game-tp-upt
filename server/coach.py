"""
AI Chess Coach endpoint.

Exposes POST /api/v1/coach/chat. The client (the Raylib in-game sidebar or any
other UI) sends the current FEN plus the full chat history; we call OpenAI
Chat Completions with a single tool — get_suggested_move — that wraps the
local Stockfish engine. The model is instructed (via SYSTEM_PROMPT) to invoke
the tool only when the player explicitly asks for a hint or a recommended
move, and to never invent concrete moves on its own.

The endpoint is gated behind the existing 'pro' subscription flag stored on
the user document, so it reuses the same Bearer-token auth as the rest of the
app — no new credential flow.

Function-calling lifecycle (see coach_chat below):

    +-----------+      tools=[get_suggested_move]      +----------------+
    | system    | --------------------------------->   | chat.completions|
    | + history |                                      | .create        |
    +-----------+                                      +-------+--------+
                                                              |
                              +-------------------------------+
                              |
                  has tool_calls?
                              |
              yes  +----------+---------+  no
                   v                    v
        run get_suggested_move   return final
        (run_chess_engine)       message.content
                   |
                   v
        append tool result -> loop (capped at MAX_TOOL_ITERATIONS)
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
DEFAULT_STOCKFISH_PATH = "./stockfish/stockfish"
ENGINE_THINK_SECONDS   = 0.8     # per get_suggested_move call
MAX_TOOL_ITERATIONS    = 4       # bound on the model<->tool loop
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


def _stockfish_path() -> str:
    return os.environ.get("STOCKFISH_PATH", DEFAULT_STOCKFISH_PATH)


# ---------------------------------------------------------------------------
# System prompt — defines the coach persona and the tool-use policy.
# ---------------------------------------------------------------------------

SYSTEM_PROMPT = """\
You are GrandCoach, a world-class chess teacher modeled after the best human masters and trainers: patient, pedagogical, encouraging, and intellectually honest. You are talking to a player who is right now in the middle of a game against a bot. Your sole purpose is to help them improve — not to win the game for them.

# Mission

Teach. Every reply should leave the player with a sharper understanding of chess, not just a correct move. Favor explanations over answers. When the player asks "what should I do?", coach them through the position before naming concrete moves.

# Reading the position

Each user turn begins with a "[Game context]" block that contains the FEN of the current board, whose turn it is, and (when available) the last move played and the bot's difficulty. Always reason from that FEN, never from memory of prior turns.

Think, in this order:
1. Material balance and piece activity.
2. King safety on both sides.
3. Pawn structure and pawn breaks.
4. Open files, weak squares, outposts.
5. Tactical motifs present or threatened: pins, forks, skewers, discovered attacks, removing the defender, overloading, back-rank issues.
6. Strategic considerations: development, the initiative, prophylaxis, the principle of two weaknesses.

If the player just made a move (provided as "Last move played"), comment on it honestly. Praise what was good. Flag inaccuracies gently and frame them as learning opportunities.

# Concrete moves: the engine is the source of truth

You DO NOT invent, guess, or recall concrete move recommendations from memory. Concrete moves come exclusively from the get_suggested_move tool, which wraps a real chess engine.

Call get_suggested_move ONLY when:
- The player explicitly asks for a hint, a suggested move, "what should I play", "show me a good move", or similar.
- The player asks you to evaluate or compare two specific candidate moves they are considering, AND you need an engine verdict to answer honestly.

DO NOT call get_suggested_move when:
- The player asks a conceptual question ("what is a fork?", "what are my weaknesses here?").
- The player asks you to describe the position, the imbalances, the plans, or the opening.
- The player asks for a critique of the last move played.
- You are simply chatting about chess principles.

When the tool returns, translate the result into teaching:
- Lead with the recommended move in coordinate notation (see format rule below).
- Explain WHY the move is good — what it threatens, what it improves, what it prevents — drawing on the principal variation the engine returned.
- Mention at most one alternative if the contrast would help the player learn.
- Never recite a centipawn score to the player. Translate it qualitatively: "a small edge for you", "roughly balanced", "a clearly winning position for Black".
- If the tool returns an error (illegal FEN, game over, engine unavailable), tell the player plainly and never fabricate a move.

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

If the engine tool returns moves in UCI like "g1f3", convert them to coordinate notation ("Ng1-f3") by reading the from-square from the board for the piece letter.

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

The chat sidebar is narrow. Long replies feel wrong in it. Be terse and useful.

Default reply shape (target ≈ 60–110 words):
1. One opening sentence that names the move (in coordinate notation) or the key idea.
2. Two or three numbered points. Each point starts with a 1–3 word **bold** label, then a colon, then a single sentence (one extra short sentence allowed only if essential).
3. (Optional) One short follow-up question — only if it actually invites a useful next exchange. Skip it otherwise.

Example shape (do not copy the content, just the shape):

    In this position, a strong move for you is **e2-e4**. Here's why:

    1. **Attacks the Knight**: It puts pressure on the knight on f6, forcing it to move.
    2. **Gains Space**: It advances your pawn in the center, increasing your control.
    3. **Opens Lines**: If the knight moves, it opens lines for your pieces.

    Want to look at any other ideas?

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
# Tool definition exposed to the model.
# ---------------------------------------------------------------------------

TOOLS: list[dict] = [{
    "type": "function",
    "function": {
        "name": "get_suggested_move",
        "description": (
            "Ask the local chess engine for the top legal candidate moves in a "
            "given position. ONLY call this when the player has explicitly "
            "asked for a hint, a recommended move, or 'what should I play?'. "
            "Never call it for conceptual questions, position descriptions, or "
            "to comment on a move that has already been played."
        ),
        "parameters": {
            "type": "object",
            "properties": {
                "fen": {
                    "type": "string",
                    "description": (
                        "FEN of the position to analyse. Use the FEN provided "
                        "in the current user turn's [Game context] block."
                    ),
                },
                "top_k": {
                    "type": "integer",
                    "minimum": 1,
                    "maximum": 3,
                    "default": 3,
                    "description": "How many candidate moves to return.",
                },
            },
            "required": ["fen"],
        },
    },
}]


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
    difficulty: Optional[Literal["easy", "medium", "hard"]] = Field(
        default=None,
        description="The bot's difficulty in the current game, if applicable."
    )
    session_id: Optional[str] = Field(
        default=None,
        description="Opaque client-side identifier; used only for server logging."
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
# Tool implementation — wraps the local UCI engine (Stockfish).
# ---------------------------------------------------------------------------

def run_chess_engine(fen: str, top_k: int = 3) -> dict:
    """
    Run the local UCI engine on the given position and return the top-k
    principal variations.

    On success the returned dict is shaped as:

        {
            "fen": <str>,
            "side_to_move": "white" | "black",
            "engine": "stockfish" | "fallback",
            "suggestions": [
                {
                    "uci": "e2e4",
                    "san": "e4",
                    "score_cp": 35,      # from STM's POV; None if forced mate
                    "mate_in": None,     # plies to mate; None if not mate
                    "principal_variation": ["e4", "e5", "Nf3"]
                },
                ...
            ]
        }

    On any failure (invalid FEN, game already over, engine crash) it returns
    a dict with an "error" key plus the offending fen. The model is told to
    surface these errors to the user rather than fabricate moves.

    If the configured Stockfish binary is missing on the filesystem we fall
    back to listing the first top_k legal moves with no score — that keeps
    the endpoint usable in dev environments without a built engine. The
    model handles a missing score gracefully (it just won't quote a
    qualitative evaluation).
    """
    top_k = max(1, min(3, int(top_k or 3)))

    try:
        board = chess.Board(fen)
    except ValueError as exc:
        return {"error": f"Invalid FEN: {exc}", "fen": fen}

    if board.is_game_over():
        outcome = board.outcome()
        return {
            "error":       "The game is already over for this position.",
            "fen":         fen,
            "outcome":     outcome.result() if outcome else None,
            "termination": outcome.termination.name if outcome and outcome.termination else None,
        }

    side = "white" if board.turn == chess.WHITE else "black"
    stockfish_path = _stockfish_path()

    # Fallback: no engine binary on disk -> return the first top_k legal moves
    # with empty scores. The model knows how to respond ("I can list legal
    # moves but I can't evaluate them right now…").
    if not os.path.exists(stockfish_path):
        fallback = []
        for mv in list(board.legal_moves)[:top_k]:
            fallback.append({
                "uci":                 mv.uci(),
                "san":                 board.san(mv),
                "score_cp":            None,
                "mate_in":             None,
                "principal_variation": [board.san(mv)],
            })
        return {
            "fen":          fen,
            "side_to_move": side,
            "engine":       "fallback",
            "suggestions":  fallback,
        }

    # Real Stockfish analysis.
    try:
        with chess.engine.SimpleEngine.popen_uci(stockfish_path) as engine:
            infos = engine.analyse(
                board,
                chess.engine.Limit(time=ENGINE_THINK_SECONDS),
                multipv=top_k,
            )
    except (chess.engine.EngineError, FileNotFoundError, OSError) as exc:
        return {"error": f"Engine failure: {exc}", "fen": fen}

    # `infos` may be a single dict if top_k == 1 — normalise.
    if isinstance(infos, dict):
        infos = [infos]

    suggestions: list[dict] = []
    for info in infos:
        pv = info.get("pv") or []
        if not pv:
            continue
        first = pv[0]
        try:
            san_first = board.san(first)
        except (chess.IllegalMoveError, AssertionError):
            continue

        scratch = board.copy()
        pv_san: list[str] = []
        for mv in pv[:8]:
            try:
                pv_san.append(scratch.san(mv))
                scratch.push(mv)
            except (chess.IllegalMoveError, AssertionError):
                break

        score_obj = info.get("score")
        pov = score_obj.pov(board.turn) if score_obj is not None else None
        suggestions.append({
            "uci":                 first.uci(),
            "san":                 san_first,
            "score_cp":            pov.score() if pov else None,
            "mate_in":             pov.mate() if pov else None,
            "principal_variation": pv_san,
        })

    if not suggestions:
        return {"error": "Engine returned no legal candidate moves.", "fen": fen}

    return {
        "fen":          fen,
        "side_to_move": side,
        "engine":       "stockfish",
        "suggestions":  suggestions,
    }


# ---------------------------------------------------------------------------
# Premium gate — reuses the existing token auth and 'subscription' field.
# ---------------------------------------------------------------------------

async def require_pro_user(authorization: Optional[str] = Header(None)) -> dict:
    """
    Validates the caller's bearer token and ensures they hold an active Pro
    subscription. 'cancelling' is treated as still-active so users retain
    access through the end of the paid period (Stripe's webhook downgrades
    them to 'free' on actual deletion).
    """
    # Lazy import to avoid a circular dependency at module load:
    # app.py imports this router, which would otherwise re-import app.py.
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


def _format_context(body: CoachRequest) -> str:
    side = body.side_to_move or _infer_side(body.fen) or "unknown"
    lines = [
        "[Game context]",
        f"Position (FEN): {body.fen}",
        f"Side to move: {side}",
    ]
    if body.last_move_san:
        lines.append(f"Last move played: {body.last_move_san}")
    if body.difficulty:
        lines.append(f"Bot difficulty: {body.difficulty}")
    return "\n".join(lines)


def _sanitize_history(msgs: list[ChatMessage]) -> list[dict]:
    """Strip anything that isn't a user/assistant turn and trim to the window."""
    trimmed = msgs[-MAX_CLIENT_HISTORY:]
    return [
        {"role": m.role, "content": m.content}
        for m in trimmed
        if m.role in ("user", "assistant") and m.content
    ]


def _attach_context(history: list[dict], body: CoachRequest) -> list[dict]:
    """
    Prepend the [Game context] block to the latest user message. If the
    client's history is empty or doesn't end with a user turn (e.g. they
    just opened the sidebar), append a synthetic context-only user turn so
    the model is prompted to greet and offer to discuss the position.
    """
    context = _format_context(body)
    if history and history[-1]["role"] == "user":
        last = dict(history[-1])
        last["content"] = f"{context}\n\n{last['content']}"
        return history[:-1] + [last]
    greeting = (f"{context}\n\n(The player just opened the coach sidebar. "
                "Greet them briefly and offer to discuss the current position.)")
    return history + [{"role": "user", "content": greeting}]


async def _dispatch_tool_call(name: str, arguments_json: str) -> dict:
    """
    Execute a single tool call emitted by the model.

    Returns the JSON-serialisable dict that becomes the 'content' field of
    the {"role":"tool", "tool_call_id":...} message we send back to OpenAI.
    Errors are returned as plain payloads so the model can surface them
    naturally to the user instead of crashing the loop.
    """
    try:
        args = json.loads(arguments_json) if arguments_json else {}
    except json.JSONDecodeError:
        return {"error": "Tool arguments were not valid JSON."}

    if name == "get_suggested_move":
        fen = args.get("fen")
        if not isinstance(fen, str) or not fen.strip():
            return {"error": "Missing required 'fen' argument."}
        top_k = args.get("top_k", 3)
        # Stockfish is synchronous IO/CPU work — offload from the event loop.
        return await asyncio.to_thread(run_chess_engine, fen, top_k)

    return {"error": f"Unknown tool: {name}"}


def _result_to_engine_analysis(result: Optional[dict]) -> Optional[EngineAnalysis]:
    """Convert the last successful run_chess_engine result into the structured
    side-channel we return to the client alongside the prose reply."""
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
    """
    Process one user turn against the AI coach.

    Function-calling loop:

      1. Validate the FEN locally (fail fast — don't burn OpenAI tokens on a
         position that doesn't parse).
      2. Build the message list:
            [ system prompt,
              ...sanitised client history (trimmed to MAX_CLIENT_HISTORY),
              latest user turn with [Game context] prefix ]
      3. Call chat.completions.create with the single tool exposed and
         tool_choice="auto" — the model decides whether to invoke it.
      4. If the response carries tool_calls, dispatch each one (currently
         only get_suggested_move -> run_chess_engine), append the assistant
         message AND each tool result back into `convo`, and loop. We cap
         at MAX_TOOL_ITERATIONS to bound cost and prevent runaway calls.
      5. The first time the model returns content without tool_calls, that
         is the final answer. We return it together with the last engine
         analysis payload (if any) so the frontend can render structured
         engine info alongside the prose.
    """
    if not body.fen:
        raise HTTPException(400, "fen is required")
    try:
        chess.Board(body.fen)
    except ValueError:
        raise HTTPException(400, "fen is not a valid FEN string")

    client = _openai_client()
    model  = _model_name()

    history = _sanitize_history(body.messages)
    history = _attach_context(history, body)

    convo: list[dict] = [{"role": "system", "content": SYSTEM_PROMPT}, *history]

    last_engine_result: Optional[dict] = None
    tool_executed = False

    try:
        for _ in range(MAX_TOOL_ITERATIONS):
            completion = await client.chat.completions.create(
                model=model,
                messages=convo,
                tools=TOOLS,
                tool_choice="auto",
                temperature=0.4,
            )
            msg = completion.choices[0].message
            # Persist the assistant turn so subsequent iterations include it.
            convo.append(msg.model_dump(exclude_none=True))

            if not msg.tool_calls:
                reply_text = (msg.content or "").strip()
                if not reply_text:
                    reply_text = "(The coach didn't have anything to add for this turn.)"
                return CoachResponse(
                    reply=reply_text,
                    tool_executed=tool_executed,
                    engine_analysis=_result_to_engine_analysis(last_engine_result),
                    model=model,
                )

            # Dispatch every tool_call the model emitted on this turn.
            for tc in msg.tool_calls:
                name      = tc.function.name
                arguments = tc.function.arguments or "{}"
                result    = await _dispatch_tool_call(name, arguments)
                if name == "get_suggested_move" and "suggestions" in result:
                    tool_executed = True
                    last_engine_result = result
                convo.append({
                    "role":         "tool",
                    "tool_call_id": tc.id,
                    "content":      json.dumps(result),
                })

        # Loop budget exhausted — model kept asking for tools without ever
        # producing a final text answer. Treat as an upstream fault.
        raise HTTPException(502, "Coach exceeded the tool-call iteration limit")

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
#
# The C/Raylib client connects here so it can type the reply out token by
# token in the chat sidebar without freezing the 60Hz UI loop.
#
# Wire format (one `data:` line per event, blank line terminates):
#     data: {"v": "<token text>"}     -- assistant content delta
#     data: {"t": "<tool name>"}      -- informational, tool was just invoked
#     data: {"e": "<error message>"}  -- recoverable/non-recoverable error
#     data: [DONE]                    -- end of stream
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
    """
    Same input contract as POST /chat but streams the assistant reply token
    by token via Server-Sent Events. Tool calls are executed transparently
    on the server; the client only sees the streamed prose plus optional
    'tool was invoked' notifications.
    """
    if not body.fen:
        raise HTTPException(400, "fen is required")
    try:
        chess.Board(body.fen)
    except ValueError:
        raise HTTPException(400, "fen is not a valid FEN string")

    client = _openai_client()
    model  = _model_name()

    history = _sanitize_history(body.messages)
    history = _attach_context(history, body)
    convo: list[dict] = [{"role": "system", "content": SYSTEM_PROMPT}, *history]

    async def event_stream():
        nonlocal convo
        try:
            for _iter in range(MAX_TOOL_ITERATIONS):
                stream = await client.chat.completions.create(
                    model=model,
                    messages=convo,
                    tools=TOOLS,
                    tool_choice="auto",
                    temperature=0.4,
                    stream=True,
                )

                # Streaming deltas trickle in. We accumulate content text plus
                # any tool_call fragments (which arrive split across many
                # chunks — OpenAI emits the JSON arguments piece by piece).
                content_acc: str = ""
                tool_acc: dict[int, dict] = {}
                finish_reason: Optional[str] = None

                async for chunk in stream:
                    if not chunk.choices:
                        continue
                    ch = chunk.choices[0]
                    delta = ch.delta

                    if delta and getattr(delta, "content", None):
                        content_acc += delta.content
                        yield _sse({"v": delta.content})

                    if delta and getattr(delta, "tool_calls", None):
                        for tc in delta.tool_calls:
                            idx = tc.index
                            acc = tool_acc.setdefault(idx, {"id": "", "name": "", "args": ""})
                            if tc.id:
                                acc["id"] += tc.id
                            if tc.function:
                                if tc.function.name:
                                    acc["name"] += tc.function.name
                                if tc.function.arguments:
                                    acc["args"] += tc.function.arguments

                    if ch.finish_reason:
                        finish_reason = ch.finish_reason

                if finish_reason == "tool_calls" and tool_acc:
                    # Replay the assistant turn (with tool_calls) into convo
                    # so the next streaming call has it in context.
                    tc_payload = []
                    for idx in sorted(tool_acc):
                        acc = tool_acc[idx]
                        tc_payload.append({
                            "id":       acc["id"],
                            "type":     "function",
                            "function": {"name": acc["name"], "arguments": acc["args"]},
                        })
                    convo.append({
                        "role":       "assistant",
                        "content":    content_acc or None,
                        "tool_calls": tc_payload,
                    })

                    # Execute each tool and feed the result back.
                    for tc in tc_payload:
                        name = tc["function"]["name"]
                        args = tc["function"]["arguments"] or "{}"
                        yield _sse({"t": name})
                        result = await _dispatch_tool_call(name, args)
                        convo.append({
                            "role":         "tool",
                            "tool_call_id": tc["id"],
                            "content":      json.dumps(result),
                        })
                    # Loop: re-stream with the tool results included.
                    continue

                # Finished cleanly with a text answer (or the model stopped
                # mid-stream — either way we're done).
                yield _sse("[DONE]")
                return

            # Tool-call loop budget exhausted.
            yield _sse({"e": "coach exceeded the tool-call iteration limit"})
            yield _sse("[DONE]")

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

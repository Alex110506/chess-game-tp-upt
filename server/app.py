"""
Chess multiplayer relay + auth/ranking — FastAPI + WebSockets + MongoDB.

Run (locally):
    export MONGODB_URI="mongodb+srv://<user>:<pass>@<cluster>/chess?retryWrites=true&w=majority"
    uvicorn app:app --host 0.0.0.0 --port 8765

Multiplayer protocol (WS, JSON over /ws):

  client -> server
    {"type": "create",  "username": "alice"}
    {"type": "join",    "code": "ABCD", "username": "bob"}
    {"type": "move",    "uci":  "e2e4"}
    {"type": "resign"}

  server -> client
    {"type": "created",       "code": "ABCD", "color": "white"}
    {"type": "joined",        "code": "ABCD", "color": "black",
                              "opponent": "alice"}
    {"type": "start",         "opponent": "bob"}
    {"type": "move",          "uci":  "e2e4"}
    {"type": "opponent_left"}
    {"type": "resign"}
    {"type": "error",         "msg":  "..."}

REST endpoints (auth & profile):
    POST /auth/register        body: {username, password}
    POST /auth/login           body: {username, password}  -> {token, profile}
    GET  /me                   header: Authorization: Bearer <token>
    POST /game/report          header + body: {result: "win"|"loss"|"draw",
                                              opponent?: "name"}
    GET  /leaderboard          (top 10 by rank)
"""

from __future__ import annotations

import asyncio
import hashlib
import hmac
import json
import os
import random
import secrets
import string
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from typing import Optional

from fastapi import FastAPI, Header, HTTPException, WebSocket, WebSocketDisconnect
from pydantic import BaseModel, Field
from motor.motor_asyncio import AsyncIOMotorClient
from pymongo.errors import DuplicateKeyError


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

try:
    from dotenv import load_dotenv
    load_dotenv()
except Exception:
    pass

MONGODB_URI = os.environ.get("MONGODB_URI", "")
MONGODB_DB = os.environ.get("MONGODB_DB", "chess")

DEFAULT_RANK = 1200      # ELO start for new accounts
ELO_K = 32               # update sensitivity
PBKDF2_ITERATIONS = 200_000


# ---------------------------------------------------------------------------
# Mongo client (lazy — handlers will fail clearly if MONGODB_URI is missing)
# ---------------------------------------------------------------------------

_mongo_client: Optional[AsyncIOMotorClient] = None


def _users():
    global _mongo_client
    if not MONGODB_URI:
        raise HTTPException(503, "Database not configured (set MONGODB_URI)")
    if _mongo_client is None:
        _mongo_client = AsyncIOMotorClient(MONGODB_URI)
    return _mongo_client[MONGODB_DB]["users"]


# ---------------------------------------------------------------------------
# Password hashing + sessions
# ---------------------------------------------------------------------------

def hash_password(password: str, salt: Optional[bytes] = None) -> tuple[str, str]:
    """Returns (hex_hash, hex_salt). Uses pbkdf2-hmac-sha256."""
    if salt is None:
        salt = secrets.token_bytes(16)
    digest = hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, PBKDF2_ITERATIONS)
    return digest.hex(), salt.hex()


def verify_password(password: str, hex_hash: str, hex_salt: str) -> bool:
    salt = bytes.fromhex(hex_salt)
    expected, _ = hash_password(password, salt)
    return hmac.compare_digest(expected, hex_hash)


def new_token() -> str:
    return secrets.token_urlsafe(32)


# ---------------------------------------------------------------------------
# ELO
# ---------------------------------------------------------------------------

def elo_update(my_rank: int, opp_rank: int, score: float) -> int:
    """Returns the new integer rank for the player who scored 'score' (1/0.5/0)."""
    expected = 1.0 / (1.0 + 10 ** ((opp_rank - my_rank) / 400.0))
    return round(my_rank + ELO_K * (score - expected))


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

def _valid_username(u: str) -> bool:
    return bool(u) and 3 <= len(u) <= 16 and all(c.isalnum() or c == "_" for c in u)


def _valid_password(p: str) -> bool:
    return bool(p) and 4 <= len(p) <= 64


# ---------------------------------------------------------------------------
# App + lifecycle
# ---------------------------------------------------------------------------

async def _ensure_indexes():
    try:
        await asyncio.wait_for(
            _users().create_index("username", unique=True), timeout=5.0)
        await asyncio.wait_for(
            _users().create_index("token", sparse=True), timeout=5.0)
    except asyncio.TimeoutError:
        print("[startup] mongo unreachable — index creation skipped")
    except Exception as e:
        print(f"[startup] index creation failed: {e}")


@asynccontextmanager
async def _lifespan(app: FastAPI):
    if MONGODB_URI:
        # fire-and-forget: don't block startup if the cluster is unreachable
        asyncio.create_task(_ensure_indexes())
    else:
        print("[startup] MONGODB_URI not set — auth endpoints will return 503")
    yield


app = FastAPI(title="Chess Multiplayer + Ranking", lifespan=_lifespan)


# ---------------------------------------------------------------------------
# Profile helpers
# ---------------------------------------------------------------------------

def _profile(doc: dict) -> dict:
    return {
        "username": doc["username"],
        "wins":     doc.get("wins", 0),
        "losses":   doc.get("losses", 0),
        "ties":     doc.get("ties", 0),
        "rank":     doc.get("rank", DEFAULT_RANK),
    }


async def _user_by_token(authorization: Optional[str]) -> dict:
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(401, "Missing bearer token")
    token = authorization[len("Bearer "):].strip()
    if not token:
        raise HTTPException(401, "Invalid token")
    doc = await _users().find_one({"token": token})
    if not doc:
        raise HTTPException(401, "Invalid or expired token")
    return doc


# ---------------------------------------------------------------------------
# Auth endpoints
# ---------------------------------------------------------------------------

class AuthIn(BaseModel):
    username: str = Field(...)
    password: str = Field(...)


@app.post("/auth/register")
async def auth_register(body: AuthIn):
    if not _valid_username(body.username):
        raise HTTPException(400, "Username must be 3-16 alphanumeric/underscore chars")
    if not _valid_password(body.password):
        raise HTTPException(400, "Password must be 4-64 chars")

    pw_hash, salt = hash_password(body.password)
    doc = {
        "username":      body.username,
        "password_hash": pw_hash,
        "salt":          salt,
        "wins":          0,
        "losses":        0,
        "ties":          0,
        "rank":          DEFAULT_RANK,
        "token":         None,
    }
    # _users() raises HTTPException(503) if MONGODB_URI is missing — let it
    # propagate so the client sees a real status. Only the duplicate-key
    # case maps to 409.
    users = _users()
    try:
        await users.insert_one(doc)
    except DuplicateKeyError:
        raise HTTPException(409, "username already taken")

    return {"ok": True}


@app.post("/auth/login")
async def auth_login(body: AuthIn):
    doc = await _users().find_one({"username": body.username})
    if not doc or not verify_password(body.password, doc["password_hash"], doc["salt"]):
        raise HTTPException(401, "Invalid credentials")
    token = new_token()
    await _users().update_one({"_id": doc["_id"]}, {"$set": {"token": token}})
    return {"token": token, "profile": _profile(doc)}


@app.post("/auth/logout")
async def auth_logout(authorization: Optional[str] = Header(None)):
    doc = await _user_by_token(authorization)
    await _users().update_one({"_id": doc["_id"]}, {"$set": {"token": None}})
    return {"ok": True}


@app.get("/me")
async def me(authorization: Optional[str] = Header(None)):
    doc = await _user_by_token(authorization)
    return _profile(doc)


# ---------------------------------------------------------------------------
# Game result reporting
# ---------------------------------------------------------------------------

class GameReportIn(BaseModel):
    result:   str                       # "win" | "loss" | "draw"
    opponent: Optional[str] = None      # opponent username (for ELO update)


@app.post("/game/report")
async def game_report(body: GameReportIn, authorization: Optional[str] = Header(None)):
    me_doc = await _user_by_token(authorization)
    if body.result not in ("win", "loss", "draw"):
        raise HTTPException(400, "result must be win/loss/draw")

    my_rank = me_doc.get("rank", DEFAULT_RANK)
    opp_rank = DEFAULT_RANK
    opp_doc = None
    if body.opponent:
        opp_doc = await _users().find_one({"username": body.opponent})
        if opp_doc:
            opp_rank = opp_doc.get("rank", DEFAULT_RANK)

    if body.result == "win":
        my_score, opp_score = 1.0, 0.0
        win_inc, loss_inc, tie_inc = 1, 0, 0
    elif body.result == "loss":
        my_score, opp_score = 0.0, 1.0
        win_inc, loss_inc, tie_inc = 0, 1, 0
    else:
        my_score, opp_score = 0.5, 0.5
        win_inc, loss_inc, tie_inc = 0, 0, 1

    new_my_rank = elo_update(my_rank, opp_rank, my_score)

    update = {
        "$set": {"rank": new_my_rank},
        "$inc": {"wins": win_inc, "losses": loss_inc, "ties": tie_inc},
    }
    await _users().update_one({"_id": me_doc["_id"]}, update)

    # also update the opponent's record if we know them — this lets the
    # winner's single report adjust both ranks. If both clients report,
    # ELO drifts only slightly because each side recomputes from the
    # pre-existing ranks; acceptable for a hobby project.
    if opp_doc:
        new_opp_rank = elo_update(opp_rank, my_rank, opp_score)
        opp_update = {
            "$set": {"rank": new_opp_rank},
            "$inc": {
                "wins":   1 if body.result == "loss" else 0,
                "losses": 1 if body.result == "win"  else 0,
                "ties":   1 if body.result == "draw" else 0,
            },
        }
        await _users().update_one({"_id": opp_doc["_id"]}, opp_update)

    fresh = await _users().find_one({"_id": me_doc["_id"]})
    return _profile(fresh)


# ---------------------------------------------------------------------------
# Leaderboard
# ---------------------------------------------------------------------------

@app.get("/leaderboard")
async def leaderboard():
    cursor = _users().find({}, {"username": 1, "wins": 1, "losses": 1, "ties": 1, "rank": 1, "_id": 0})
    cursor = cursor.sort("rank", -1).limit(10)
    return {"top": [doc async for doc in cursor]}


@app.get("/")
async def root():
    return {
        "service": "chess",
        "rooms": len(rooms),
        "auth": bool(MONGODB_URI),
    }


# ---------------------------------------------------------------------------
# Multiplayer relay (WebSocket)
# ---------------------------------------------------------------------------

@dataclass
class Room:
    code:   str
    host:   WebSocket
    guest:  Optional[WebSocket] = None
    host_user:  Optional[str] = None
    guest_user: Optional[str] = None
    lock:   asyncio.Lock = field(default_factory=asyncio.Lock)


rooms: dict[str, Room] = {}
rooms_lock = asyncio.Lock()


def _new_code() -> str:
    alphabet = string.ascii_uppercase + string.digits
    while True:
        code = "".join(random.choices(alphabet, k=4))
        if code not in rooms:
            return code


async def _send(ws: WebSocket, payload: dict) -> None:
    try:
        await ws.send_text(json.dumps(payload))
    except Exception:
        pass


async def _peer(room: Room, me: WebSocket) -> Optional[WebSocket]:
    return room.guest if me is room.host else room.host


def _peer_username(room: Room, me: WebSocket) -> Optional[str]:
    return room.guest_user if me is room.host else room.host_user


async def _drop_room(code: str) -> None:
    async with rooms_lock:
        rooms.pop(code, None)


@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()
    my_room: Optional[Room] = None

    try:
        while True:
            raw = await ws.receive_text()
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                await _send(ws, {"type": "error", "msg": "invalid json"})
                continue

            mtype = msg.get("type")

            if mtype == "create":
                if my_room is not None:
                    await _send(ws, {"type": "error", "msg": "already in room"})
                    continue
                async with rooms_lock:
                    code = _new_code()
                    room = Room(code=code, host=ws, host_user=msg.get("username"))
                    rooms[code] = room
                my_room = room
                await _send(ws, {"type": "created", "code": code, "color": "white"})

            elif mtype == "join":
                if my_room is not None:
                    await _send(ws, {"type": "error", "msg": "already in room"})
                    continue
                code = (msg.get("code") or "").upper()
                async with rooms_lock:
                    room = rooms.get(code)
                    if room is None:
                        await _send(ws, {"type": "error", "msg": "room not found"})
                        continue
                    if room.guest is not None:
                        await _send(ws, {"type": "error", "msg": "room full"})
                        continue
                    room.guest = ws
                    room.guest_user = msg.get("username")
                my_room = room
                await _send(ws, {
                    "type": "joined",
                    "code": code,
                    "color": "black",
                    "opponent": room.host_user or "",
                })
                await _send(room.host, {"type": "start", "opponent": room.guest_user or ""})
                await _send(ws,        {"type": "start", "opponent": room.host_user or ""})

            elif mtype in ("move", "resign"):
                if my_room is None:
                    await _send(ws, {"type": "error", "msg": "not in room"})
                    continue
                peer = await _peer(my_room, ws)
                if peer is None:
                    await _send(ws, {"type": "error", "msg": "no opponent yet"})
                    continue
                await _send(peer, msg)

            else:
                await _send(ws, {"type": "error", "msg": f"unknown type: {mtype}"})

    except WebSocketDisconnect:
        pass
    except Exception:
        pass
    finally:
        if my_room is not None:
            peer = await _peer(my_room, ws)
            if peer is not None:
                await _send(peer, {"type": "opponent_left"})
            await _drop_room(my_room.code)

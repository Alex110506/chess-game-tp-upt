"""
Chess multiplayer relay — FastAPI + WebSockets.

Run:
    uvicorn app:app --host 0.0.0.0 --port 8765

Protocol (JSON over WS, one message per frame):

  client -> server
    {"type": "create"}
    {"type": "join", "code": "ABCD"}
    {"type": "move", "uci": "e2e4"}      # promotions: "e7e8q"
    {"type": "resign"}

  server -> client
    {"type": "created",       "code": "ABCD", "color": "white"}
    {"type": "joined",        "code": "ABCD", "color": "black"}
    {"type": "start"}                                  # both sides
    {"type": "move",          "uci": "e2e4"}           # forwarded
    {"type": "opponent_left"}
    {"type": "resign"}                                  # forwarded
    {"type": "error",         "msg": "..."}
"""

import asyncio
import json
import random
import string
from dataclasses import dataclass, field
from typing import Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

app = FastAPI(title="Chess Multiplayer Relay")


@dataclass
class Room:
    code: str
    host: WebSocket
    guest: Optional[WebSocket] = None
    lock: asyncio.Lock = field(default_factory=asyncio.Lock)


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


async def _drop_room(code: str) -> None:
    async with rooms_lock:
        rooms.pop(code, None)


@app.get("/")
async def root():
    return {"service": "chess-multiplayer", "rooms": len(rooms)}


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
                    room = Room(code=code, host=ws)
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
                my_room = room
                await _send(ws, {"type": "joined", "code": code, "color": "black"})
                await _send(room.host, {"type": "start"})
                await _send(ws, {"type": "start"})

            elif mtype in ("move", "resign"):
                if my_room is None:
                    await _send(ws, {"type": "error", "msg": "not in room"})
                    continue
                peer = await _peer(my_room, ws)
                if peer is None:
                    await _send(ws, {"type": "error", "msg": "no opponent yet"})
                    continue
                # forward verbatim — server is a dumb relay; client validates moves
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

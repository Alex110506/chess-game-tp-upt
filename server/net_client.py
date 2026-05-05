"""
Bridge: stdin/stdout (parent C process) <-> WebSocket (relay server).

Spawned by chess_gui as a subprocess. Each side reads/writes single
JSON objects, one per line. The bridge forwards them verbatim — no
protocol logic lives here.

Args:
    --url ws://host:port/ws   (default: ws://127.0.0.1:8765/ws)

stdout is unbuffered text; stderr is for diagnostics only.
"""

import argparse
import asyncio
import json
import sys

import websockets


async def stdin_to_ws(ws):
    loop = asyncio.get_running_loop()
    reader = asyncio.StreamReader()
    protocol = asyncio.StreamReaderProtocol(reader)
    await loop.connect_read_pipe(lambda: protocol, sys.stdin)
    while True:
        line = await reader.readline()
        if not line:
            break
        try:
            text = line.decode("utf-8", errors="replace").strip()
            if not text:
                continue
            # validate it parses; forward original text
            json.loads(text)
            await ws.send(text)
        except json.JSONDecodeError:
            print(f'{{"type":"error","msg":"bad json from parent"}}', flush=True)
        except Exception as e:
            print(f'{{"type":"error","msg":"send failed: {e}"}}', flush=True)
            break


async def ws_to_stdout(ws):
    async for msg in ws:
        if isinstance(msg, bytes):
            msg = msg.decode("utf-8", errors="replace")
        # ensure single line
        msg = msg.replace("\n", " ").replace("\r", " ")
        sys.stdout.write(msg + "\n")
        sys.stdout.flush()


async def run(url: str):
    try:
        async with websockets.connect(url, ping_interval=20, ping_timeout=20) as ws:
            print('{"type":"connected"}', flush=True)
            t1 = asyncio.create_task(stdin_to_ws(ws))
            t2 = asyncio.create_task(ws_to_stdout(ws))
            done, pending = await asyncio.wait({t1, t2}, return_when=asyncio.FIRST_COMPLETED)
            for t in pending:
                t.cancel()
    except Exception as e:
        print(f'{{"type":"error","msg":"connect failed: {e}"}}', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="ws://127.0.0.1:8765/ws")
    args = ap.parse_args()

    # line-buffered stdout
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except Exception:
        pass

    try:
        asyncio.run(run(args.url))
    except KeyboardInterrupt:
        pass
    print('{"type":"closed"}', flush=True)


if __name__ == "__main__":
    main()

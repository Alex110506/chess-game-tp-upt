#!/usr/bin/env bash
# Launch the chess multiplayer relay server.
set -e
cd "$(dirname "$0")"
exec ./.venv/bin/uvicorn app:app --host 0.0.0.0 --port 8765 "$@"

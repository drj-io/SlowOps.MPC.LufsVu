#!/usr/bin/env bash
# Redraw every skin PNG and the docs/images previews (Pillow in a local venv under build/).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PY="${PILLOW_PY:-build/venv/bin/python}"
if [ ! -x "$PY" ]; then
    python3 -m venv build/venv
    build/venv/bin/pip install -q pillow
    PY=build/venv/bin/python
fi
"$PY" scripts/make_skin_art.py

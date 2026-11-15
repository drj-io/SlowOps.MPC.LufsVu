#!/usr/bin/env bash
# Build everything, then zip the MPC folder into dist/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
cd "$ROOT"
"$ROOT/scripts/build.sh"
mkdir -p dist
python3 "$SDK/tools/package.py" plugin --out dist
ls -l dist

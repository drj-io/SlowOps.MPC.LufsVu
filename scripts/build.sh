#!/usr/bin/env bash
# Build the MPC plugin folder (native + arm32 + arm64 + skins + validation) and the test tools.
#   ./scripts/build.sh                 everything
#   ./scripts/build.sh --target native native only (fast; enough for the tests)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
PY="${PYTHON:-python3}"
cd "$ROOT"
[ -f "$SDK/tools/build.py" ] || git submodule update --init third_party/mpc-plugin-builder-sdk
# 1) native build first: writes plugin/build/params.json, which the skin generator needs
"$PY" "$SDK/tools/build.py" plugin --target native
# 2) regenerate TUI.json / Q-Links.json from the real parameter list (art PNGs are committed)
"$PY" scripts/gen_skins.py
# 3) full build (copies skins, cross-compiles, validates)
"$PY" "$SDK/tools/build.py" plugin "$@"
mkdir -p build
c++ -std=c++17 -O2 -Wall -Wextra -I core/include tests/core_tests.cpp -o build/core_tests
c++ -std=c++17 -O2 -Wall -I "$SDK/framework" tests/scenario_host.cpp -o build/scenario_host
c++ -std=c++17 -O2 -Wall -I core/include tests/lufs_crosscheck.cpp -o build/lufs_crosscheck
echo
echo "MPC folder: $ROOT/plugin/build/Slow Ops - VST - LUFS VU"

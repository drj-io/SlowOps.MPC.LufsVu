#!/usr/bin/env bash
# Full verification: build, core unit tests, SDK host simulator, scenario host on the real binary.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
LIB="$ROOT/plugin/build/native/vu-meter.dylib"
[ "$(uname)" = "Darwin" ] || LIB="${LIB%.dylib}.so"
cd "$ROOT"
"$ROOT/scripts/build.sh" "$@"
echo; echo "== core engine tests"; ./build/core_tests
echo; echo "== SDK host simulator"
"$SDK/.sdk-tools/host_sim" "$LIB" test --seconds 4 --wav plugin/build/hostsim.wav
echo; echo "== scenario host (real plugin binary)"
./build/scenario_host "$LIB" plugin/build
echo; echo "== skin validation"
python3 scripts/check_skins.py
echo; echo "ALL CHECKS PASSED"

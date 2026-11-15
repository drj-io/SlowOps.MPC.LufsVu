#!/usr/bin/env bash
# Install the built plugin on an MPC over the network (developer mode / SSH must be on).
#   MPC_ADDRESS=192.168.1.167 MPC_DRIVE=3 ./scripts/deploy.sh            install / replace
#   MPC_ADDRESS=192.168.1.167 MPC_DRIVE=3 ./scripts/deploy.sh --restart  ...then restart the MPC app
# The MPC only rereads plugin skins when its app restarts; restarting discards unsaved work on the MPC.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/mpc-plugin-builder-sdk"
ADDR="${MPC_ADDRESS:?set MPC_ADDRESS to the IP address of the MPC}"
DRIVE_ARGS=()
[ -n "${MPC_DRIVE:-}" ] && DRIVE_ARGS=(--drive "$MPC_DRIVE")
cd "$ROOT"
python3 "$SDK/tools/mpc_remote.py" "$ADDR" "${DRIVE_ARGS[@]}" --yes update plugin
if [ "${1:-}" = "--restart" ]; then
    python3 "$SDK/tools/mpc_remote.py" "$ADDR" --yes restart
fi

#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game=$(realpath -- "${1:?Usage: tools/picross/play.sh GAME_DIRECTORY}")
export PICROSS_EARLY_HEAP=1 PICROSS_INIT_VAMM=1 EOWRECOMP_SYNC_SHADERS=1
export EOWRECOMP_DATA_ROOT=${EOWRECOMP_DATA_ROOT:-"$root/.picrecomp/runtime"}
exec "$root/build/eowrecomp" --play "$game"

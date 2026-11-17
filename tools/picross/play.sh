#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game=$(realpath -- "${1:?Usage: tools/picross/play.sh GAME_DIRECTORY}")
export PICRECOMP_SYNC_SHADERS=1
export PICRECOMP_DATA_ROOT=${PICRECOMP_DATA_ROOT:-"$root/.picrecomp/runtime"}
exec "$root/build/picrecomp" --play "$game"

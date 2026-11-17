#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game=${1:+$(realpath -- "$1")}  # (optional: a prepared game folder to start from)
export PICRECOMP_DATA_ROOT=${PICRECOMP_DATA_ROOT:-"$root/.picrecomp/runtime"}
exec "$root/build/picrecomp" ${game:+"$game"}  # opens the start screen; Play boots the game (PICRECOMP_NO_LAUNCHER=1 skips it)

#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
game=$(realpath -- "${1:?Usage: tools/picross/generate.sh GAME_DIRECTORY}")
python=${PYTHON:-"$root/tools/venv/bin/python"}
[[ -f "$game/exefs/main.npdm" && -f "$game/romfs.bin" ]]
if [[ -e generated || -L generated ]]; then
    echo "Move the existing generated directory aside before regenerating." >&2
    exit 1
fi
mkdir -p "$root/.picrecomp"
stage=$(mktemp -d "$root/.picrecomp/generate.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
ln -s "$game/exefs" "$stage/exefs"
PYTHONPATH="$root/tools" "$python" - "$stage/exefs/main" <<'PY'
import sys
from gen import Module

main = Module("main", sys.argv[1], 0x7000000000, "c")
if main.nso.build_id != "260797018fefb8459d1bf07815e71c23":
    raise SystemExit(f"Unsupported PICROSS S build ID: {main.nso.build_id}")
PY
for spec in main:0x7000000000 subsdk0:0x7008000000 sdk:0x700c000000; do
    module=${spec%%:*}
    "$python" tools/gen.py "$module" "$stage/exefs/$module" "${spec#*:}" "$stage/lift/$module" \
        --hle runtime/hle/hle_symbols.txt --simd c
done
# (the jump tables of main are resolved by gen.py itself: tools/test_jumptable.py)
"$python" tools/picross/fix-exception-resume.py "$stage"
mv -- "$stage/lift" generated

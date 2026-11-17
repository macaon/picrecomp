#!/usr/bin/env bash
# Lifts a prepared game's three modules to C: GAME_DIRECTORY/generated (never committed). The game's manifest in titles/
# is found by its main build ID; the exception-landing patches come from it.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
game=$(realpath -- "${1:?Usage: tools/picross/generate.sh GAME_DIRECTORY}")
python=${PYTHON:-"$root/tools/venv/bin/python"}
[[ -f "$game/exefs/main.npdm" && -f "$game/romfs.bin" ]]
if [[ -e "$game/generated" || -L "$game/generated" ]]; then
    echo "Remove $game/generated before regenerating." >&2
    exit 1
fi
stage=$(mktemp -d "$game/.generate.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
ln -s "$game/exefs" "$stage/exefs"
PYTHONPATH="$root/tools" "$python" - "$stage/exefs/main" "$stage" <<'PY'
import glob, json, sys
from gen import Module

main = Module("main", sys.argv[1], 0x7000000000, "c")
titles = [json.load(open(f)) for f in sorted(glob.glob("titles/*.json"))]
title = next((t for t in titles if t["main_build_id"] == main.nso.build_id), None)
if title is None:
    raise SystemExit(f"Unsupported build ID {main.nso.build_id}: no manifest in titles/")
print(f"Generating {title['name']}")
json.dump(title, open(sys.argv[2] + "/title.json", "w"))
open(sys.argv[2] + "/build-id", "w").write(main.nso.build_id + "\n")
PY
for spec in main:0x7000000000 subsdk0:0x7008000000 sdk:0x700c000000; do
    module=${spec%%:*}
    "$python" tools/gen.py "$module" "$stage/exefs/$module" "${spec#*:}" "$stage/lift/$module" \
        --hle runtime/hle/hle_symbols.txt --simd c
done
# (the jump tables of main are resolved by gen.py itself: tools/test_jumptable.py)
"$python" tools/picross/fix-exception-resume.py "$stage" "$stage/title.json"
mv -- "$stage/lift" "$game/generated"
mv -- "$stage/build-id" "$game/generated/build-id"

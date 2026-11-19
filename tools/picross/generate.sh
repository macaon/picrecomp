#!/usr/bin/env bash
# Lifts a prepared game's three modules to C: GAME_DIRECTORY/generated (never committed). Exception landings, the compact
# relocations and the unwinder's resume are all the generator's own work (tools/gen.py), whatever the game's version.
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
for spec in main:0x7000000000 subsdk0:0x7008000000 sdk:0x700c000000; do
    module=${spec%%:*}
    "$python" tools/gen.py "$module" "$game/exefs/$module" "${spec#*:}" "$stage/lift/$module" \
        --hle runtime/hle/hle_symbols.txt --simd c
done
# (the jump tables of main are resolved by gen.py itself: tools/test_jumptable.py)
# the build ID the library is checked against at start: the first 16 bytes of the main module's id, as the runtime reads it
"$python" -c 'import sys; print(open(sys.argv[1], "rb").read()[0x40:0x50].hex())' "$game/exefs/main" > "$stage/build-id"
mv -- "$stage/lift" "$game/generated"
mv -- "$stage/build-id" "$game/generated/build-id"

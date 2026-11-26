#!/usr/bin/env bash
# No game files or GPU needed.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
out="$root/build/test_shader_policy"
mkdir -p "$out"
${CC:-clang} -D_GNU_SOURCE -O1 -fno-strict-aliasing -ffunction-sections -fdata-sections \
    -Wno-format -Iruntime -Iruntime/nvn $(pkg-config --cflags sdl3) \
    "$root/tools/test_shader_policy.c" "$root/runtime/settings.c" \
    -Wl,--gc-sections $(pkg-config --libs sdl3) -lpthread -lm -o "$out/test_shader_policy"
"$out/test_shader_policy"

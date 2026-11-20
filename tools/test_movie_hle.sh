#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/test_movie_hle"
mkdir -p "$out"
${CC:-clang} -D_GNU_SOURCE -std=c11 -O2 -Wall -Wextra \
    -Wno-format -Wno-unused-function -Wno-unused-const-variable -Wno-unused-parameter \
    -ffunction-sections -fdata-sections "$root/tools/test_movie_hle.c" \
    "$root/runtime/hle/movie_source.c" -Wl,--gc-sections \
    $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale) -lpthread \
    -o "$out/test_movie_hle"
"$out/test_movie_hle"

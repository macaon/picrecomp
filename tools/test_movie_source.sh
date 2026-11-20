#!/usr/bin/env bash
# Generates its own video; no game files are used by this regression test.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/test_movie_source"
mkdir -p "$out"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'testsrc=size=64x48:rate=12:duration=1' \
    -an -c:v mpeg4 -pix_fmt yuv420p "$out/pattern.mp4"
${CC:-clang} -std=c11 -O2 -Wall -Wextra -Werror \
    "$root/tools/test_movie_source.c" "$root/runtime/hle/movie_source.c" \
    $(pkg-config --cflags --libs libavformat libavcodec libavutil) -o "$out/test_movie_source"
"$out/test_movie_source" "$out/pattern.mp4"

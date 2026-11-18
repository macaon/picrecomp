#!/bin/bash
# Test of runtime/nvn/texpatch.c (the face-button prompts turned to the bound controller button): builds
# tools/test_texpatch.c against it into build/ and runs it. Usage: tools/test_texpatch.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/texpatch_test"
mkdir -p "$OUT"
clang -O1 -g -Wall -I"$ROOT/runtime" -I"$ROOT/runtime/nvn" $(pkg-config --cflags sdl3) \
    "$ROOT/tools/test_texpatch.c" "$ROOT/runtime/nvn/texpatch.c" -o "$OUT/test_texpatch"
"$OUT/test_texpatch"

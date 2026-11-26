#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mkdir -p build/test_physical_memory
${CC:-clang} -D_GNU_SOURCE -O1 -g -fno-strict-aliasing -ffunction-sections -fdata-sections \
    -Wno-format -Iruntime -Iruntime/nvn $(pkg-config --cflags sdl3) tools/test_physical_memory.c \
    -Wl,--gc-sections $(pkg-config --libs sdl3) -lpthread -lm -o build/test_physical_memory/test
build/test_physical_memory/test

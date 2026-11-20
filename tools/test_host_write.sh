#!/bin/sh
# Test for runtime/services/host_write.h (see tools/test_host_write.c)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/test
${CC:-clang} -O2 -Wall tools/test_host_write.c -o build/test/test_host_write
build/test/test_host_write

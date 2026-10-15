#!/bin/bash
# Test Android's platform code on Linux, including old-kernel mmap behavior.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/android_platform_test"
mkdir -p "$OUT"
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror -rdynamic -pthread \
    "$ROOT/tools/test_android_platform.c" "$ROOT/runtime/platform/android.c" \
    -Wl,--wrap=mmap -Wl,--wrap=munmap -ldl -o "$OUT/test_android_platform"
"$OUT/test_android_platform"

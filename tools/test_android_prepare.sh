#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/android_prepare_test
"${CC:-cc}" -D_DEFAULT_SOURCE -std=c11 -O2 -Wall -Wextra -Werror -Iruntime tools/test_android_prepare.c \
    runtime/platform/android_prepare.c runtime/romfs_build.c -o build/android_prepare_test/test
build/android_prepare_test/test

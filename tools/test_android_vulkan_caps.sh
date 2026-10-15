#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/android_vulkan_test
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -Iandroid/deps/arm64-v8a/include \
    runtime/nvn/vulkan/tests/android_caps_test.c -o build/android_vulkan_test/test
build/android_vulkan_test/test

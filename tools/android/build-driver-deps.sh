#!/usr/bin/env bash
# Build rootless per-app Turnip loader support; no GPU driver binary is bundled.
set -euo pipefail
cd "$(dirname "$0")/../.."
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to Android NDK r27c}"
TASK_ROOT=$PWD
TASK_SRC=$TASK_ROOT/android/deps/src
TASK_PREFIX=$TASK_ROOT/android/deps/arm64-v8a
TASK_BUILD=$TASK_ROOT/android/deps/build/driver
TASK_JOBS=${JOBS:-4}
mkdir -p "$TASK_SRC" "$TASK_PREFIX/include" "$TASK_PREFIX/share/licenses" "$TASK_BUILD"
fetch_commit() {
    local name=$1 url=$2 commit=$3
    if [ ! -d "$TASK_SRC/$name/.git" ]; then
        git init "$TASK_SRC/$name"
        git -C "$TASK_SRC/$name" remote add origin "$url"
    fi
    if ! git -C "$TASK_SRC/$name" cat-file -e "$commit^{commit}" 2>/dev/null; then
        git -C "$TASK_SRC/$name" fetch --depth 1 origin "$commit"
    fi
    git -C "$TASK_SRC/$name" checkout --detach "$commit"
    test "$(git -C "$TASK_SRC/$name" rev-parse HEAD)" = "$commit"
}
fetch_commit libadrenotools https://github.com/bylaws/libadrenotools.git 8fae8ce254dfc1344527e05301e43f37dea2df80
# Pins liblinkernsbypass to aa3975893d83ef1bc84c321ec60c65fbf1287887.
git -C "$TASK_SRC/libadrenotools" submodule update --init --recursive
fetch_commit volk https://github.com/zeux/volk.git 0dc3ce00bf98b9f0b6fe708ca0f7eb74e2830173
cmake -S "$TASK_SRC/libadrenotools" -B "$TASK_BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON -DGEN_INSTALL_TARGET=ON -DCMAKE_INSTALL_PREFIX="$TASK_PREFIX" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$TASK_BUILD" -j "$TASK_JOBS"
cmake --install "$TASK_BUILD"
cp "$TASK_SRC/volk/volk.h" "$TASK_PREFIX/include/"
cp "$TASK_SRC/libadrenotools/LICENSE" "$TASK_PREFIX/share/licenses/libadrenotools-BSD-2-Clause.txt"
cp "$TASK_SRC/libadrenotools/lib/linkernsbypass/LICENSE" "$TASK_PREFIX/share/licenses/liblinkernsbypass-license.txt"
cp "$TASK_SRC/volk/LICENSE.md" "$TASK_PREFIX/share/licenses/volk-MIT.txt"
mkdir -p "$TASK_ROOT/android/deps/driver-jni/arm64-v8a"
for hook in main_hook hook_impl file_redirect_hook gsl_alloc_hook; do
    cp "$TASK_PREFIX/lib/lib${hook}.so" "$TASK_ROOT/android/deps/driver-jni/arm64-v8a/"
done
printf 'Android Vulkan provider dependencies ready: %s\n' "$TASK_PREFIX"

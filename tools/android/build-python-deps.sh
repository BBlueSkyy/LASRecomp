#!/usr/bin/env bash
# Reproducible Android ARM64 embedded generator dependencies. No game data.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to Android NDK r27c or newer}"
CMAKE=${CMAKE:-cmake}
JOBS=${JOBS:-4}
SRC="$ROOT/android/deps/src"
PREFIX="$ROOT/android/deps/arm64-v8a/python"
CACHE="$SRC/python-embed-download"
mkdir -p "$CACHE" "$PREFIX/lib"
fetch() {
    local filename=$1 url=$2 hash=$3
    if [[ ! -f "$CACHE/$filename" ]]; then
        curl --fail --location --retry 3 "$url" -o "$CACHE/$filename"
    fi
    echo "$hash  $CACHE/$filename" | sha256sum --check --status || {
        echo "SHA256 mismatch: $filename (remove cache entry and retry)" >&2; exit 1;
    }
}
fetch python.tar.gz https://www.python.org/ftp/python/3.14.8/python-3.14.8-aarch64-linux-android.tar.gz 11324313957da3736f44e90257304d288864321a4dd376ed0cc35a54eacf9a33
fetch unicorn-2.1.4.tar.gz https://files.pythonhosted.org/packages/b2/1b/b4248aa8422e86de690cf8e85cf8feae4c33405a097d1ebe71570bdaa6f5/unicorn-2.1.4.tar.gz 00567a70e323f749b419cd86bee4f9115beab7ebba32194581c090cbb7c59cff
fetch capstone-5.0.9.tar.gz https://files.pythonhosted.org/packages/39/01/6516910f546fbb996068207b9dd0229b14bc8dae223114d5e0e27d3cad11/capstone-5.0.9.tar.gz 0429af292ddc604d3c9344696807e281d5e728db029f00e6a4ea9e3bff1aac9e
mkdir -p "$SRC/cpython-3.14.8"
tar --no-same-owner -xzf "$CACHE/python.tar.gz" -C "$SRC/cpython-3.14.8"
for item in unicorn-2.1.4 capstone-5.0.9; do
    tar --no-same-owner -xzf "$CACHE/$item.tar.gz" -C "$SRC"
done
common=(-DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384)
"$CMAKE" -S "$SRC/unicorn-2.1.4/src" -B "$SRC/unicorn-android-build" "${common[@]}" \
    -DBUILD_SHARED_LIBS=ON -DUNICORN_ARCH=aarch64 -DUNICORN_BUILD_TESTS=OFF \
    -DUNICORN_LEGACY_STATIC_ARCHIVE=OFF -DUNICORN_INSTALL=ON
"$CMAKE" --build "$SRC/unicorn-android-build" --target unicorn --parallel "$JOBS"
# Android upstream intentionally uses an unversioned SONAME.
cp "$SRC/unicorn-android-build/libunicorn.so" "$PREFIX/lib/libunicorn.so"
"$CMAKE" -S "$SRC/capstone-5.0.9/src" -B "$SRC/capstone-android-build" "${common[@]}" \
    -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DCAPSTONE_ARCHITECTURE_DEFAULT=OFF \
    -DCAPSTONE_ARM64_SUPPORT=ON -DCAPSTONE_BUILD_TESTS=OFF -DCAPSTONE_BUILD_CSTOOL=OFF
"$CMAKE" --build "$SRC/capstone-android-build" --target capstone --parallel "$JOBS"
cp -L "$SRC/capstone-android-build/libcapstone.so" "$PREFIX/lib/libcapstone.so"
python3 "$ROOT/tools/android/package-python-assets.py" --root "$ROOT"
echo "Embedded Python ready: $PREFIX"

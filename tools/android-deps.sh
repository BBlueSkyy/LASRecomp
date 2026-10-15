#!/usr/bin/env bash
# Build all Android runtime dependencies from source. No game input is used.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to Android NDK r27c}"
ROOT=$PWD
SRC=$ROOT/android/deps/src
PREFIX=$ROOT/android/deps/arm64-v8a
BUILD=$ROOT/android/deps/build
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
mkdir -p "$SRC" "$PREFIX/include/vma" "$BUILD"
fetch_git() {
    local name=$1 url=$2 ref=$3
    if [ ! -d "$SRC/$name/.git" ]; then git clone --depth 1 --branch "$ref" "$url" "$SRC/$name"; fi
    test "$(git -C "$SRC/$name" rev-parse HEAD)" = "$(git -C "$SRC/$name" rev-parse "$ref^{commit}")"
}
fetch_git SDL https://github.com/libsdl-org/SDL.git release-3.4.16
fetch_git Vulkan-Headers https://github.com/KhronosGroup/Vulkan-Headers.git vulkan-sdk-1.4.350.1
fetch_git VulkanMemoryAllocator https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.3.0
fetch_git shaderc https://github.com/google/shaderc.git v2026.3
cp -R "$SRC/Vulkan-Headers/include/vulkan" "$SRC/Vulkan-Headers/include/vk_video" "$PREFIX/include/"
cp "$SRC/VulkanMemoryAllocator/include/vk_mem_alloc.h" "$PREFIX/include/vma/"
TC=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake
COMMON=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$TC" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib)
if [ ! -f "$PREFIX/lib/libshaderc_shared.so" ]; then
    (cd "$SRC/shaderc" && python3 utils/git-sync-deps)
    cmake -S "$SRC/shaderc" -B "$BUILD/shaderc" "${COMMON[@]}" \
        -DANDROID_STL=c++_shared -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON \
        -DSHADERC_SKIP_COPYRIGHT_CHECK=ON -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON \
        -DENABLE_GLSLANG_BINARIES=OFF -DSPIRV_TOOLS_BUILD_STATIC=ON -DBUILD_SHARED_LIBS=OFF
    cmake --build "$BUILD/shaderc" --target shaderc_shared -j "$JOBS"
    mkdir -p "$PREFIX/lib" "$PREFIX/include/shaderc"
    cp "$BUILD/shaderc/libshaderc/libshaderc_shared.so" "$PREFIX/lib/"
    cp "$SRC/shaderc/libshaderc/include/shaderc/"*.h "$PREFIX/include/shaderc/"
fi
if [ ! -d "$SRC/ffmpeg" ]; then
    curl -fL --retry 3 https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz -o "$SRC/ffmpeg.tar.xz"
    echo "8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e  $SRC/ffmpeg.tar.xz" | sha256sum -c -
    mkdir -p "$SRC/ffmpeg"
    tar --no-same-owner -xf "$SRC/ffmpeg.tar.xz" -C "$SRC/ffmpeg" --strip-components=1
fi
if [ ! -f "$PREFIX/lib/libavcodec.a" ]; then
    HOST=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin
    mkdir -p "$BUILD/ffmpeg"
    (cd "$BUILD/ffmpeg" && "$SRC/ffmpeg/configure" --prefix="$PREFIX" \
        --target-os=android --arch=aarch64 --enable-cross-compile \
        --cc="$HOST/aarch64-linux-android28-clang" --cxx="$HOST/aarch64-linux-android28-clang++" \
        --ar="$HOST/llvm-ar" --nm="$HOST/llvm-nm" --ranlib="$HOST/llvm-ranlib" --strip="$HOST/llvm-strip" \
        --enable-static --disable-shared --enable-pic --disable-asm \
        --disable-autodetect --disable-programs --disable-doc --disable-network --disable-avdevice --disable-avfilter \
        --disable-swresample --disable-everything --enable-protocol=file --enable-demuxer=matroska,mov \
        --enable-decoder=vp9,vp8,vorbis,opus,h264,aac --enable-parser=vp9,vp8,vorbis,opus,h264,aac \
        && make -j "$JOBS" && make install)
fi
if grep -qE 'enable-gpl|enable-nonfree' "$PREFIX/lib/pkgconfig/libavcodec.pc"; then
    echo 'Refusing GPL/nonfree FFmpeg build' >&2; exit 1
fi
printf 'Android runtime dependencies ready: %s\n' "$PREFIX"

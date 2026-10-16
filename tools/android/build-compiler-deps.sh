#!/usr/bin/env bash
# Build Android-hosted Clang/LLVM/LLD libraries. No compiler executable is shipped.
# RECOVERED CHECKPOINT: this build was started but never verified to completion.
set -euo pipefail
cd "$(dirname "$0")/../.."
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to Android NDK r27c}"
ROOT=$PWD
SRC=$ROOT/android/deps/src/llvm-project
BUILD=$ROOT/android/deps/build
PREFIX=$ROOT/android/deps/arm64-v8a/compiler
JOBS=${COMPILER_JOBS:-3}
LLVM_COMMIT=3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff
CMAKE=${CMAKE:-cmake}
NINJA=${NINJA:-ninja}
NDK_HOST=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64
mkdir -p "$(dirname "$SRC")" "$BUILD" "$PREFIX/lib" "$PREFIX/include"
if [ ! -d "$SRC/.git" ]; then
    git clone --depth 1 --branch llvmorg-18.1.8 --filter=blob:none --sparse https://github.com/llvm/llvm-project.git "$SRC"
    git -C "$SRC" sparse-checkout set llvm clang lld cmake third-party
fi
test "$(git -C "$SRC" rev-parse HEAD)" = "$LLVM_COMMIT"
COMMON=(-G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    '-DLLVM_ENABLE_PROJECTS=clang;lld' -DLLVM_TARGETS_TO_BUILD=AArch64 -DLLVM_INCLUDE_TESTS=OFF
    -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF
    -DCLANG_INCLUDE_TESTS=OFF -DLLD_INCLUDE_TESTS=OFF -DLLVM_ENABLE_BINDINGS=OFF
    -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_TERMINFO=OFF
    -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_RTTI=OFF -DLLVM_ENABLE_EH=OFF -DLLVM_ENABLE_ASSERTIONS=OFF
    -DLLVM_BUILD_TOOLS=OFF -DCLANG_BUILD_TOOLS=OFF -DLLD_BUILD_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF
    -DLLVM_PARALLEL_LINK_JOBS=1 -DCMAKE_C_FLAGS_RELEASE=-O2\ -DNDEBUG -DCMAKE_CXX_FLAGS_RELEASE=-O2\ -DNDEBUG)
# Only these table generators execute on the build host; all shipped code is ARM64.
"$CMAKE" -S "$SRC/llvm" -B "$BUILD/compiler-host" "${COMMON[@]}" \
    -DCMAKE_C_COMPILER="$NDK_HOST/bin/clang" -DCMAKE_CXX_COMPILER="$NDK_HOST/bin/clang++"
"$CMAKE" --build "$BUILD/compiler-host" --target llvm-tblgen clang-tblgen -j "$JOBS"
"$CMAKE" -S "$SRC/llvm" -B "$BUILD/compiler-arm64" "${COMMON[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DLLVM_HOST_TRIPLE=aarch64-linux-android \
    -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-android28 \
    -DLLVM_TABLEGEN="$BUILD/compiler-host/bin/llvm-tblgen" \
    -DCLANG_TABLEGEN="$BUILD/compiler-host/bin/clang-tblgen" \
    -DLLVM_NATIVE_TOOL_DIR="$BUILD/compiler-host/bin"
"$CMAKE" --build "$BUILD/compiler-arm64" --target clangFrontendTool lldELF LLVMAArch64CodeGen LLVMAArch64AsmParser LLVMAArch64Desc LLVMAArch64Info -j "$JOBS"
# Link these archives as a rescan group. Unused members are not included in the app.
find "$BUILD/compiler-arm64/lib" -maxdepth 1 -name '*.a' -exec cp '{}' "$PREFIX/lib/" \;
for project in llvm clang lld; do cp -R "$SRC/$project/include/." "$PREFIX/include/"; done
cp -R "$BUILD/compiler-arm64/include/." "$PREFIX/include/"
cp -R "$BUILD/compiler-arm64/tools/clang/include/." "$PREFIX/include/"
printf '%s\n' "$LLVM_COMMIT" > "$PREFIX/llvm-revision.txt"
python3 tools/android/package_compiler_assets.py --ndk "$ANDROID_NDK_HOME" --llvm-source "$SRC" \
    --out "$ROOT/android/deps/compiler-assets"
printf 'Android embedded compiler libraries: %s\nCompiler assets: %s\n' "$PREFIX" "$ROOT/android/deps/compiler-assets"

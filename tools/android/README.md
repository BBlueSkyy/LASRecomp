# Android game module boundary

`build_module.py` is a **host development tool** for verifying the Android runtime
against the real static recompiler. It does not make the APK self-contained and
is not an end-user setup workflow. No game files or generated game code are
included here.

```sh
python3 -m pip install unicorn capstone lz4
python3 tools/android/build_module.py \
  --ndk "$ANDROID_NDK_HOME" \
  --runtime-lib /path/to/apk-build/arm64-v8a/liblasrecomp.so \
  --game-dir /path/to/legally-extracted-game
```

The tool runs the existing `tools/gen.py` for all functions in `exefs/main` and
`exefs/sdk`, explicitly using `--simd asm`, and uses the NDK's ARM64 Android Clang
to compile and link. SDK HLE names come from the existing `hle_symbols.txt`.
`--no-undefined` requires every runtime import to link against the actual APK's
`liblasrecomp.so`. The output is `lasgame.so` and a matching `.id` sidecar. RomFS
assets are still read by the normal game loader and are not compiled into this
module. `--out` and `--work-dir` allow a separate artifact and temporary location.

`gamelib_id.py` supplies the same fingerprint to Android CMake and this tool. It
hashes the desktop Makefile's generator/ABI inputs plus the Android compiler/link
contract. It is intentionally distinct from a desktop-generated module's ID.
The module uses PIC, hidden generated symbols, an exported `lasgame_tables`
entry, and 16 KB ELF load alignment. `GameLibTables` and `GuestFn(Cpu *)` retain
their existing ABI.

The Android loader promotes its already-loaded runtime to `RTLD_GLOBAL`, then
opens the module using `RTLD_NOW` so unresolved imports fail before dispatch.
It checks the ELF class, endianness, architecture, load ranges, stack/segment
permissions, text relocations, and exported entry before `dlopen`. It then checks
the ABI/ID and all counts, table pointers, sorted aligned guest addresses, and
native function pointers against the module's readable/executable ELF mappings.
This prevents malformed table dereferences; it is not a sandbox for arbitrary
native code. Failed loads can be retried after a successful rebuild.

## What is missing for the self-contained APK

The repository does not yet contain Android builds of the following components,
or an in-process bridge connecting them to the setup UI:

1. Embedded ARM64 Android CPython, its required standard library, and the original
   generator Python modules plus `rando/patch_variants.json`.
2. ARM64 Android Unicorn and matching Python bindings, including its AArch64
   emulation support used by the generator's jump-table analysis.
3. The `lz4.block` extension built for that exact Android Python ABI, used to
   decompress NSO segments, plus Capstone's matching Python/native bindings
   (`gen.py` imports `analyze.py`, which imports Capstone).
4. An ARM64 Android-hosted compiler/linker callable inside the app: for example,
   LLVM/Clang and LLD linked behind an in-process C bridge, together with their
   resource headers, Android target sysroot, CRT objects, and compiler runtime.
5. A native build coordinator that invokes those embedded libraries, preserves
   the existing generator output/patches and flags, reports progress and errors,
   cancels safely, and publishes a complete module and matching ID before launch.

The downloaded NDK's `linux-x86_64` Clang and a desktop Python/PyInstaller helper
are host programs. Packaging them as APK assets does not provide these Android
components. `runtime/gamebuild.c` currently uses the desktop subprocess contract;
this helper deliberately does not claim to implement an embedded compiler bridge.

## Tests without game assets

```sh
CC=clang python3 tools/android/test_module.py
```

This compiles a real shared runtime with `-Wl,-z,global`, links synthetic modules
against it with strict import resolution, loads them with the production loader,
and calls a synthetic guest function through `GameLibTables`. The call must
resolve `rt_ticks` from the shared runtime. Tests cover invalid ABI/ID, empty and
overflowing counts, unsorted tables, unmapped table/function pointers, unresolved
imports, malformed/wrong-architecture ELF, retry after failure, and fingerprint
changes for every ABI input. The fixture contains no game data.

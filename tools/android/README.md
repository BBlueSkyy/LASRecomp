# Android game module boundary

`build_module.py` is a **host development tool** for verifying the Android runtime
against the real static recompiler. It does not make the APK self-contained and
is not an end-user setup workflow. No game files or generated game code are
included here.

```sh
python3 -m pip install unicorn capstone lz4
python3 tools/android/build_module.py \
  --ndk "$ANDROID_NDK_HOME" \
  --apk /path/to/LASRecomp.apk \
  --game-dir /path/to/legally-extracted-game
```

The tool runs the existing `tools/gen.py` for all functions in `exefs/main` and
`exefs/sdk`, explicitly using `--simd asm`, and uses the NDK's ARM64 Android Clang
to compile and link. SDK HLE names come from the existing `hle_symbols.txt`.
`--no-undefined` requires every runtime import to link against the actual APK's
`liblasrecomp.so`. The output is `lasgame.so` and a matching `.id` sidecar. RomFS
assets are still read by the normal game loader and are not compiled into this
module. `--out` and `--work-dir` allow a separate artifact and temporary location.
`--apk` extracts only the ARM64 runtime library into temporary host storage;
manual library extraction is unnecessary. `--runtime-lib` remains supported.
Use the same generator/ABI sources as the APK. The builder reads the exported
`lasrecomp_build_contract_v1` data from the actual runtime ELF without executing
ARM64 code on the host. It selects that APK's guest base and refuses a source
fingerprint mismatch before starting generation. `--guest-base` is an optional
assertion against the APK, including custom `-PguestBase=0x...` layouts. APKs
predating the exported contract must be rebuilt from this branch.
The generated module and sidecar are private outputs: do not upload them to public
Actions artifacts or commit them.

## Import the private module into the APK

The launcher accepts an externally generated private ARM64 module. Build it against
this APK's `liblasrecomp.so` and matching source/configuration, then copy
`lasgame.so` and `lasgame.so.id` into one folder accessible to Android's document
picker. In the launcher, choose **Importar módulo privado ARM64 (pasta)** and
select that folder. This copies only the module pair into private storage; it
does not recopy the game's RomFS. Import the extracted `exefs/` and `romfs/`
separately, then choose **Iniciar runtime com módulo importado**.

Alternatively, place both module files alongside `exefs/` and `romfs/` and use
the ordinary full game-data import. A separately imported module takes precedence
over a module included with the game-data tree. Both imports stage complete
copies before replacing the previous files. Missing sidecars, non-ARM64 ELF
headers and interrupted provider reads leave the previous import intact.

Import checks the module header and sidecar format. The native runtime still
checks the actual ELF, imports, ABI fingerprint and function tables before guest
execution; presence in the launcher does not certify compatibility or gameplay.
An imported pair starts the runtime directly even in an experimental embedded
setup build. Without a pair, that build retains the separate preparation process.

The Android guest layout is part of the generated-module contract. Modules from
an earlier layout or fingerprint must be regenerated for the corresponding APK;
the current Android builder defaults to the low guest layout beginning at
`0x1000000000`. A desktop module, or merely editing its `.id`, cannot substitute
for rebuilding the ARM64 module. No Zelda boot or frame has been verified by the
synthetic import tests.

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

## Experimental on-device setup

The default APK retains the host-built module workflow. An opt-in implementation
now connects `PreparationActivity` in a separate `:prepare` process to the original
generator and native compiler callbacks. It remains **WIP and disabled by default**:
neither the complete Android-hosted LLVM build nor an end-to-end preparation run
has been verified. Do not describe this option as a working self-contained APK.

The implementation uses:

- Isolated embedded CPython with installed public stdlib/generator assets; native
  extensions load from Android's extracted native library directory. Source and
  hash-pinned packaging scripts build Unicorn/Capstone; NSO LZ4 decoding uses a
  bounded Python implementation instead of a writable executable extension.
- The existing `gen.py`/lifter/patches, both NSOs and `--simd asm`; generation runs
  in process, with progress/errors returned through JNI.
- Android-hosted Clang cc1 and LLD linked as libraries. Generated objects link
  with `--no-undefined` against the APK's installed `liblasrecomp.so`, API28 CRT
  and import libraries. No external compiler executable is invoked by this path.
- A cache keyed by full NSO SHA-256 and module ABI fingerprint, with output hash,
  architecture checks, an exclusive preparation lock and marker-last publication.
  The production loader still performs the full ELF/table validation.
- A staged versioned asset installation and Binder death handshake: `:game` starts
  after `:prepare` exits so the compiler's mappings are removed before guest
  memory reservation. This does not solve existing ART/vendor address collisions.

Experimental build steps (large native dependency builds; not yet validated):

```sh
tools/android-deps.sh
tools/android/build-python-deps.sh
tools/android/build-compiler-deps.sh
python3 tools/android/stage-embedded-assets.py
gradle -p android :app:assembleDebug -PembeddedSetup=true
```

See [compiler recovery status](COMPILER_CHECKPOINT.md). The ordinary GitHub Actions
APK does not enable this flag or package these unverified optional dependencies.
The NDK's `linux-x86_64` compiler is used only during development and build/test;
it is never shipped as an Android executable.

The independent custom Vulkan option uses `tools/android/build-driver-deps.sh`
and `-PcustomVulkan=true`. It requires user-imported compatible Turnip libraries;
no driver is bundled. Both optional integrations still need native build and
hardware validation.

## Tests without game assets

```sh
CC=clang python3 tools/android/test_module.py
CC=clang python3 tools/test_runtime_boot.py
python3 tools/android/test_compilation_cache.py
CC=clang python3 tools/android/test_embedded_generation.py
```

This compiles a real shared runtime with `-Wl,-z,global`, links synthetic modules
against it with strict import resolution, loads them with the production loader,
and calls a synthetic guest function through `GameLibTables`. The call must
resolve `rt_ticks` from the shared runtime. Tests cover invalid ABI/ID, empty and
overflowing counts, unsorted tables, unmapped table/function pointers, unresolved
imports, malformed/wrong-architecture ELF, retry after failure, and fingerprint
changes for every ABI input. The fixture contains no game data.

The generator test needs host `unicorn==2.1.4`, `capstone==5.0.9`, `lz4==4.4.5`.
It executes lifted synthetic code on the host against original runtime sources.
With an NDK it also validates real ARM64 compilation/linking through the cache
coordinator, using host compiler callbacks. It does not test embedded LLVM or
execute Nintendo code.

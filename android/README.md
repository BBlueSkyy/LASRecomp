# LASRecomp Android ARM64 runtime port

This development APK links the original runtime. It is **not a self-contained playable port yet**. An experimental on-device generator/compiler path is implemented behind a default-off build flag; its Android dependency build and execution have not been verified end to end. No real game frame has been verified on Android. No Nintendo files or generated game code are included.

## Build

Use JDK17, SDK35, NDK27.2.12479018, CMake3.22.1 and Gradle8.9. On Linux:

```sh
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.2.12479018"
tools/android-deps.sh
gradle -p android :app:assembleDebug
```

The dependency script builds pinned SDL3, shaderc and LGPL FFmpeg, and installs Vulkan/VMA headers. CMake links the complete loader, kernel, dispatch, HLE/services, NVN command processing, Maxwell shader translation, Vulkan renderer, ImGui and SDL audio/input. The APK contains ARM64 libraries only. Native libraries use 16KB-compatible ELF alignment. The standard Android build and its private game module use the same relocated identity-address layout starting at `0x1000000000`, while desktop builds retain the original layout.

The workflow `.github/workflows/android.yml` builds dependencies, runs host regressions, builds the APK and checks exported SDL entry/game-code absence. The artifact is `LASRecomp-Android-arm64-runtime-dev`.

## Actual implemented paths

- SAF imports extracted `exefs/` and `romfs/` recursively into private storage, with staged replacement and rollback. NSO/build-ID checks require the existing v1.0.0 static recompiler input. The URI is never treated as a native path.
- The launcher separately imports an owner's externally generated `lasgame.so` and `lasgame.so.id` from a selected folder, without recopying RomFS. The full extraction import also accepts the pair beside `exefs/` and `romfs/`. An imported pair bypasses experimental embedded preparation and goes through the production runtime loader; see the [private module workflow](../tools/android/README.md#import-the-private-module-into-the-apk). Modules must be regenerated for the APK's matching guest layout and ABI fingerprint.
- `GameActivity` runs in `:game`, loads a lightweight memory platform before SDL/heavy runtime, and invokes the original runtime through `SDL_main`.
- Bionic platform uses actual pthreads, monotonic timing, TLS signal stacks, fault reporting and unwind backtraces. Runtime callbacks are registered before guest threads start.
- Exact guest reservation safely supports old kernels and reports conflicting VMAs. Reset keeps ownership and discards only owned anonymous pages. No foreign Android mapping is overwritten.
- The original RomFS builder can prepare the raw `romfs.bin` consumed by services after module preflight succeeds.
- Real Android Vulkan presentation uses the original NVN renderer. Required feature bits and extension dependencies are negotiated; missing capabilities are rejected. See [renderer contract](../runtime/nvn/vulkan/ANDROID.md).
- SDL handles Android surface/lifecycle, physical gamepads and native audio. Touch controls feed the same keyboard bindings as the existing runtime; menus retain SDL touch input. The launcher exposes the last runtime log.
- The loader validates ARM64 ELF, module ABI/ID, sorted address tables and executable function pointers. The original generator can build an Android module using the host-only [development helper](../tools/android/README.md).

## Concrete remaining blockers

1. **On-device generation and compilation validation:** the opt-in implementation connects embedded CPython, the original generator, Unicorn/Capstone, a Python LZ4 decoder, Clang/LLD C++ callbacks, SHA-256 cache publication and a separate `:prepare` Activity. Dependency build/packaging scripts are checked in. The LLVM archives and compiler bridge have not completed a verified build/link or run on Android; the normal APK therefore keeps this disabled. See [embedded setup](../tools/android/README.md#experimental-on-device-setup).
2. **Guest memory availability on real Android devices:** the guest layout was relocated consistently across the runtime, generator and ABI fingerprint to `0x1000000000..0x1320000000` by `e89ce33`. The exact reservation still must be tested on the target device; if that range is occupied by ART/vendor mappings, startup fails safely and a further layout change or real guest-to-host address translation is required. No foreign mapping is overwritten.
3. **Device Vulkan capabilities/custom drivers:** API1.1 is accepted only with equivalent required features/extensions. The Moto G100 proprietary driver has not been proven to meet those requirements. A default-off libadrenotools/volk provider and SAF driver import are implemented. The [experimental Turnip workflow](../.github/workflows/android-turnip.yml) separately builds/tests a custom-provider APK without bundling any GPU driver; this still needs device validation. See [renderer contract](../runtime/nvn/vulkan/ANDROID.md).
4. **Android gameplay validation:** actual guest boot, graphics, audio, controls and pause/resume remain unverified on hardware. Linking code is not evidence that the game runs.

## First real guest-frame evidence on Android

For a development-only test, import legally extracted game data and a private ARM64
`lasgame.so` plus `lasgame.so.id` built **for this exact APK and guest base**. Start
the game runtime from the launcher. No game data or generated proprietary module is
part of either CI artifact.

The real NVN/Vulkan game's first accepted swapchain submission (not the synthetic
diagnostic clear or a reboot black frame) emits:

```
[android] first guest NVN frame submitted to the Android swapchain
```

When the presentation surface supports transfer-source readback, the first guest
submission also saves `first_guest_frame.png` privately under Android's app data
directory. In the launcher, tap **Ver primeira imagem do jogo (captura privada)** to
view that capture. The previous capture is removed when a new game launch starts,
so a stale image cannot be mistaken for a newly rendered frame.

These prove only that the runtime used the guest NVN presentation path and submitted
pixels for inspection. A black/wrong image, missing readback, or a successful
swapchain present does **not** prove that Zelda's title screen is visible or
that any gameplay works. Actual device/game validation remains necessary.

An independent [Turnip-enabled APK pipeline](../.github/workflows/android-turnip.yml)
builds the optional provider separately from the standard system-Vulkan APK.
It does not bundle Turnip itself: the user must provide a compatible driver ZIP
and select it inside the experimental launcher.

## Tests and diagnostics

```sh
tools/test_android_platform.sh
tools/test_android_prepare.sh
tools/test_android_vulkan_caps.sh
python3 tools/android/test_module.py
CC=clang python3 tools/test_runtime_boot.py
python3 tools/android/test_compilation_cache.py
# Install pinned host Unicorn/Capstone/LZ4 in a venv before this test.
CC=clang python3 tools/android/test_embedded_generation.py
android/tests/run.sh
```

These use synthetic fixtures, not game data. The separate diagnostic screen retains the old synthetic Vulkan clear; it is never counted as a game frame. Capability tests and the optional host software-Vulkan smoke test do not validate Android game rendering.

The generator regression executes an original-generator lifted synthetic function on the host. With `ANDROID_NDK_HOME`, it also builds an ARM64 runtime subset from original sources, compiles/links the complete synthetic generated module through the cache coordinator, and verifies that a cache hit skips compilation. This uses the host NDK CLI, not the embedded Clang/LLD bridge, and does not execute the ARM64 output.

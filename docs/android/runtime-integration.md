# Android runtime integration

The requested outcome is a self-contained Android ARM64 application importing the user's extracted Link's Awakening v1.0.0 files and executing the existing static recompiler runtime. No proprietary input or generated game code may ship. Continue PR #1 in place, never main or merge.

## Design

Build the complete original runtime as liblasrecomp.so with SDL3's Android entry point, the existing HLE/kernel/loader/dispatch, NVN Vulkan, ImGui and FFmpeg. Use SDLActivity in a separate game process so the UI and guest lifetime are independent. Import SAF documents transactionally into private storage; all settings, saves and caches use that storage. Keep the original desktop builds unchanged.

Identity guest addresses are an existing ABI throughout generated code, HLE and rendering. Exact reservation must never replace Android mappings. Reserve before runtime boot, report overlapping mappings, and reset only owned memory. Arbitrary relocation is not valid until every guest dereference can be translated.

Negotiate actual Vulkan features and extension equivalents; backend calls must use the matching entry points and shader target. Unsupported devices fail with specific capability information.

Game code remains a separate module with the existing GameLibTables ABI. Loading a valid ARM64 module is a development validation path, not the requested final user workflow. An on-device generator still needs Python/Unicorn/LZ4 and an embedded ARM64 compiler/linker; do not call the APK self-contained until these are bundled and connected.

## Implementation plan

- [x] Add Bionic platform, safe reservation and host regression tests (collision preservation, reset and range checks).
- [x] Build pinned Android SDL3, FFmpeg, Vulkan headers/VMA and shaderc, then link all original runtime sources.
- [x] Add SDL main entry with private paths, explicit preflight and clean failure reporting; keep desktop constructor/re-exec behavior desktop-only.
- [x] Add transactional SAF import, SDL lifecycle and physical/touch input.
- [x] Negotiate Vulkan feature/extension chains and dispatch; test capability selection without a game.
- [x] Validate game module architecture/ABI/table integrity; retain generator compatibility.
- [x] Cross-compile and link Android ARM64 locally; run host tests and document remaining blockers.
- [ ] Publish only port/android-arm64; the push initiates the Android workflow, with no subsequent build polling.

## Review focus

Foreign mappings must survive reservation/reset errors. Imported trees must reject traversal and interrupted imports must preserve the previous copy. Unsupported Vulkan devices must not reach unavailable entry points. Missing/invalid game modules must not trigger fake boot success. Native code loading and SAF permissions must remain explicit Android behavior.

## Review decisions

The native link exposed garbage collection of the original ELF HLE/service/reset registration sections. The Android target retains start/stop sections with `-z nostart-stop-gc`. Bionic FORTIFY also exposed main.c's undersized realpath buffer; it now uses PATH_MAX. A desktop regression found during review was corrected by compiling gamelib_validate.o with the same module ID as gamelib.o. Android CMake tracks the fingerprint inputs so incremental builds refresh that ID.

## Validation in this execution

- Full native Android API28/ARM64 cross-build and strict link succeeded (SDL3, actual HLE/service/reset sections, FFmpeg, NVN/Vulkan, ImGui).
- Host platform tests, actual RomFS fixture construction, mocked Vulkan capability/dispatch tests, native module import/ABI/ELF tests with a real computed fingerprint, and Java import transaction tests passed.
- Full application and SDL Java compile against SDK35 passed. D8 and AAPT2 accepted the classes/manifest; a local package assembly check included all six native shared libraries, verified their 16KB ELF load alignment, and included no game code.
- Host software Vulkan device creation, promoted commands and timeline queries passed for requested API1.1/1.2/1.3. This is not Android surface/game testing.
- Gradle's local dependency resolution was blocked by this environment's unavailable Java network/proxy path. Native compilation and independent Java/DEX/manifest packaging were verified; the full Gradle APK workflow is delegated to the explicitly requested GitHub Actions run, without polling it after push.
- No Android device, game boot, gameplay, audio output or real first frame was available for validation. The original synthetic clear remains separate diagnostics.

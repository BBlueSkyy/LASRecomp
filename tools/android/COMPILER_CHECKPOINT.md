Embedded compiler recovery checkpoint
====================================

This optional implementation is **unverified and must remain disabled by default**.
It is not evidence that the APK can compile or run a game on a device.

Recovered from the interrupted session:

- `build-compiler-deps.sh` pins LLVM/Clang/LLD 18.1.8 commit
  `3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff`, builds host table generators,
  then cross-builds AArch64 static libraries with NDK r27c and three jobs.
- `package_compiler_assets.py` packages the measured public header closure,
  runtime ABI headers, API 28 import stubs/CRT, compiler-rt and license notices.
  It includes no game input or target compiler executable.

Prepared during recovery, not previously validated:

- `embedded_compiler.cpp/.h` implements in-process Clang driver to cc1 execution
  and LLD ELF linking, serialized behind a mutex, with fatal failure poisoning.
- `test_embedded_compiler.cpp` is an opt-in fixture for C syntax diagnostics,
  compilation after an error, and repeated AArch64 shared-module links against
  an installed runtime library. It has not been built or executed.

Integration contract: `CompilerPaths.asset_root` points to the extracted
compiler-assets directory. `native_library_dir` is the Android application's
native library directory containing `liblasrecomp.so`. The application must
execute preparation separately and restart that process after fatal LLVM/LLD
failures. Link the compiler's static archives as a rescan group, with generated
and source include trees collected under `android/deps/arm64-v8a/compiler`.

Evidence available at recovery: shell syntax checking and Python bytecode
compilation passed. The earlier source checkout finished, and a build command
was launched, but no successful completed build or end-to-end compiler test was
observed. The reset removed the source checkout and build outputs. Neither an
NDK/LLVM download nor a replacement long build was attempted during recovery.

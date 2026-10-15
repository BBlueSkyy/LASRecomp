# LASRecomp Android — ARM64 port (milestone 1)

This is a **diagnostic APK**, not a playable game. No Nintendo assets, ROM,
game code, or user game dumps are contained in the repository or release.

Build with JDK 17, Android SDK 35, NDK 27.2.12479018, CMake 3.22.1 and Gradle 8.9:

    cd android
    gradle :app:assembleDebug

Output: android/app/build/outputs/apk/debug/app-debug.apk (path from repo root).

The Android activity currently:
- probes the upstream guest address range defined in runtime/rt.h (12.5 GiB
  virtual reservation at the exact guest address without MAP_FIXED clobbering);
- enumerates actual Vulkan devices and API versions;
- creates a genuine Android Surface / Vulkan swapchain and submits one
  **synthetic solid-color image**, not a game frame;
- persists a Storage Access Framework permission for the user's extracted
  v1.0.0 directory and checks exefs/ and romfs/ exist.

The runtime has not been linked into the app yet. Remaining work includes:
1. SDL3 Android integration and original Vulkan game backend.
2. Android/Bionic implementation of platform abstraction: threads, signals,
   address-space setup, reset tables, lifecycle and paths.
3. Android-compatible game module loading while retaining GAMELIB_ID/ABI.
4. Host-side generation/compilation of guest code from the player's own
   legally obtained Link's Awakening v1.0.0 files without Termux.
5. File import, input, FFmpeg, audio, real game boot and rendering.

**Never interpret a SAF document ID as a filesystem path.** The current
file picker only stores a URI. No game assets are copied or generated.

The synthetic frame should be reported as a Vulkan host-backend test only.
Linux aarch64 remains experimental upstream, so Android gameplay is unproven.

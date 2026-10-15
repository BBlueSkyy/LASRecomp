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


## Moto G100: initial results and corrected probe

The first test on the Moto G100 reported EEXIST when attempting to reserve
the original 0x7000000000..0x7320000000 guest window, and Qualcomm's system
Vulkan 1.1.128 driver. This does NOT mean physical RAM exhaustion. The revised
diagnostic lists conflicting /proc/self/maps mappings, and safely probes
four alternative 12.5-GiB virtual windows without overwriting any mappings.

The Vulkan swapchain-only test now accepts Vulkan 1.0+ with
VK_KHR_swapchain rather than incorrectly requiring Vulkan 1.3 for a solid-color
clear. The **original game renderer still requires Vulkan 1.3**; a successful
solid-color frame on Qualcomm's proprietary Vulkan 1.1 driver is only proof
that Android Surface/presentation works. The future game port needs a Vulkan
1.3-capable driver such as a compatible user-provided Turnip build, together
with a legitimate per-app custom driver loader, or a separately validated
Vulkan 1.1 renderer fallback.

An alternative available virtual range is not automatically usable. The
upstream runtime/generator assume guest addresses equal host pointers and
recompilation-address constants. Relocating the guest address-space requires a
coherent architecture decision, not a one-line change to GUEST_LO.

# Android Vulkan backend

Android builds use the same renderer, command executor, GLSL translator, texture uploads, postprocessing and
ImGui renderer as the desktop Vulkan build. SDL3 owns the Android window and creates `VkSurfaceKHR` using
its Android video backend. No diagnostic renderer substitutes for game rendering.

`device.c` negotiates the minimum of the loader and physical device API, with Vulkan 1.1 as the minimum.
`android_caps.h` rejects a device before selection unless it has the following renderer requirements:

| Functionality | Core version | Required extension on older APIs |
| --- | --- | --- |
| Presentation | Extension | `VK_KHR_swapchain` |
| Timeline submissions/fences | 1.2 | `VK_KHR_timeline_semaphore` |
| Mirror-clamp samplers | 1.2 | `VK_KHR_sampler_mirror_clamp_to_edge` |
| Dynamic rendering | 1.3 | `VK_KHR_dynamic_rendering` |
| Rendering dependencies on 1.1 | 1.2 | `VK_KHR_depth_stencil_resolve`, `VK_KHR_create_renderpass2` |
| Submission/barrier/timestamp commands | 1.3 | `VK_KHR_synchronization2` |
| Dynamic pipeline state | 1.3 | `VK_EXT_extended_dynamic_state`, `VK_EXT_extended_dynamic_state2` |

Feature bits are queried before enabling them, including optional custom border colours, instance divisors
and device-fault diagnostics. Shader address arithmetic requires `shaderInt64`; translated shuffles require
basic, shuffle and relative-shuffle subgroup operations in fragment and compute stages. The base requirements
also include robust buffers, independent blending and vertex/fragment shader stores and atomics.

Android targets Vulkan 1.1/SPIR-V 1.3 shaders, with a separate cache key from desktop shaders. This preserves
specialized compute workgroup sizes through the `WorkgroupSize` constant and fragment discard through `OpKill`.
It does not require the desktop target's unused 8/16-bit storage, scalar layout, demote/terminate, or
`maintenance4` features. VMA and ImGui receive the negotiated device API; promoted entry points are resolved
with their KHR/EXT names where appropriate. ASTC/ETC2 sampling features are enabled when available.

SDL lifecycle events stop acquiring background window images. Foreground or surface-loss recovery recreates
the SDL surface on the main thread and the swapchain on the renderer thread. Android image acquisition is
nonblocking; `VK_TIMEOUT` and `VK_NOT_READY` mean there is no image available for this frame. No new sleep or
elapsed-time cutoff is used. Swapchain usage, alpha mode, extent and optional screenshot readback follow
the surface's advertised capabilities.

## Custom Turnip drivers

The default build uses Android's system Vulkan loader. Optional
`LARECOMP_ANDROID_CUSTOM_VULKAN` enables the provider in `runtime/platform/android_vulkan.c`.
`tools/android/build-driver-deps.sh` builds pinned libadrenotools and volk sources and preserves their licenses.
No custom GPU driver binary is bundled. This path still requires Android-device validation; keep the build
option disabled for release builds until tested on supported Adreno hardware.

When enabled, volk routes every renderer command through one provider. VMA receives the provider's instance
and device loaders; ImGui uses `IMGUI_IMPL_VULKAN_NO_PROTOTYPES` and the same loaders. The renderer creates
Android surfaces directly from SDL's native-window property, avoiding SDL's separate system Vulkan loader.
Both providers retain the same capability checks, lifecycle handling, and promoted-command resolution.
A selected custom provider must report Mesa Turnip's driver ID; vendor fallback is rejected.

The application must set these variables before SDL/native renderer startup:

| Variable | Value |
| --- | --- |
| `LARECOMP_VK_DRIVER` | `system` (default) or `custom` |
| `LARECOMP_VK_DRIVER_DIR` | App-private directory holding an extracted arm64 Turnip driver and dependencies |
| `LARECOMP_VK_DRIVER_NAME` | Driver `.so` basename, without a path |
| `LARECOMP_ANDROID_NATIVE_LIB_DIR` | `ApplicationInfo.nativeLibraryDir` |
| `LARECOMP_VK_TMP_DIR` | Writable application cache directory; required for Android 9 |

The provider retains its loader handle for the process lifetime. Changing the selected driver requires a
process restart. Package libadrenotools and its hook shared libraries using `useLegacyPackaging=true`, as
required by upstream. Driver files must be imported into private application storage rather than loaded from
shared storage. Loading failures are reported instead of retrying with a different provider.

## Focused checks

`tests/android_caps_test.c` is a host Clang test with mocked Vulkan queries; it needs Vulkan and VMA headers,
but no Vulkan library or GPU. It covers Vulkan 1.1/1.2/1.3 chains, required and optional features, dependency
extensions, rejection paths and all 17 promoted command resolutions.

`tests/android_driver_smoke.c` additionally links the host Vulkan loader and exercises real device creation,
dispatch and timeline queries at all three APIs. Use a compatible driver through `VK_ICD_FILENAMES`; a
software driver is sufficient. It does not create an Android window or validate game rendering.

`tests/android_provider_smoke.c` uses volk for all commands and must be compiled with `volk.c` and `-ldl`
(without `-lvulkan`). It exercises the same capability/device/timeline contract through complete dispatch.
This host test does not validate libadrenotools namespaces, Android WSI or lifecycle behavior.

`tools/android/test_vulkan_build.py` cross-compiles the renderer, VMA and ImGui Vulkan backend for Android
ARM64/API28 in both system and optional provider configurations. It needs NDK r27c, the SDL/Vulkan/VMA/shaderc
headers prepared by the normal dependency setup, and the sources/libraries from `build-driver-deps.sh`.
The shaderc source checkout's public headers can be used before building the shaderc library.

```sh
ANDROID_NDK_HOME=/path/to/android-ndk-r27c python3 tools/android/test_vulkan_build.py
```

The check combines the real renderer objects and rejects unresolved Vulkan symbols in the provider build;
volk must supply the complete dispatch. It also strictly links the standalone provider with libadrenotools
and rejects a `libvulkan.so` dependency. Both configurations and the pinned loader dependency build have
passed this cross-compile check with NDK r27c. This does not link the complete runtime/APK, load an imported
driver on Android, create a surface, or establish that a Zelda frame has rendered. Those remain device and
game-data validation steps.

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

The current build uses Android's system Vulkan loader. Loading a Turnip `.so` by changing a version check or
passing it to `dlopen` does not create a supported custom-driver path. Android linker namespaces, the driver's
dependencies and its entry points need to be handled together, typically by an explicitly integrated
`libadrenotools` implementation. No custom driver is bundled or silently selected.

A future driver provider must initialize before creating the SDL window/Vulkan instance and own the driver
handle plus `vkGetInstanceProcAddr` for the renderer's lifetime. The Android renderer must route **all** core
Vulkan calls through that provider, extend the promoted-command table in `vk.h` into a complete dispatch
table, and pass the provider's loaders to VMA and ImGui's `IMGUI_IMPL_VULKAN_NO_PROTOTYPES` path. SDL also needs
the same provider, through a supported forwarding Vulkan-loader library or an explicit SDL Android integration;
SDL's public loader currently accepts a library path rather than an existing driver handle. Mixing a custom
instance with directly linked system-loader commands is invalid. Stock and custom providers must run the
same capability checks and surface lifecycle. This integration remains separate work requiring Android-device
validation; the existing dispatch table by itself does not implement custom Turnip loading.

## Focused checks

`tests/android_caps_test.c` is a host Clang test with mocked Vulkan queries; it needs Vulkan and VMA headers,
but no Vulkan library or GPU. It covers Vulkan 1.1/1.2/1.3 chains, required and optional features, dependency
extensions, rejection paths and all 17 promoted command resolutions.

`tests/android_driver_smoke.c` additionally links the host Vulkan loader and exercises real device creation,
dispatch and timeline queries at all three APIs. Use a compatible driver through `VK_ICD_FILENAMES`; a
software driver is sufficient. It does not create an Android window or validate game rendering.

// Host-only test of the Android renderer's capability contract and promoted-command dispatch.
// Build with Clang (the runtime uses Clang builtins), -std=c11 -I<Vulkan-Headers/include> -I<VMA/include>.
// No Vulkan library, Android toolchain or GPU is required.
#define __ANDROID__ 1
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../vk.h"

VkG g_vk;
static uint32_t test_api;
static const char *missing_extension;
static VkStructureType missing_feature;
static bool optional_supported, bad_subgroups;
static unsigned loaded;
static const char *const extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
    VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME, VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
    VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME, VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME,
    VK_EXT_DEVICE_FAULT_EXTENSION_NAME
};

void rt_fatal_trace(void) { abort(); }
static bool has_ext(VkExtensionProperties *ep, uint32_t n, const char *name) {
    for (uint32_t i = 0; i < n; i++) if (!strcmp(ep[i].extensionName, name)) return true;
    return false;
}
#include "../android_caps.h"

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char *layer,
                                                                   uint32_t *count, VkExtensionProperties *out) {
    (void)pd; (void)layer;
    uint32_t n = 0;
    for (size_t i = 0; i < sizeof extensions / sizeof extensions[0]; i++) {
        if (missing_extension && !strcmp(missing_extension, extensions[i])) continue;
        if (out) { memset(&out[n], 0, sizeof out[n]); strcpy(out[n].extensionName, extensions[i]); }
        n++;
    }
    *count = n;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2 *f) {
    (void)pd;
    f->features.shaderInt64 = VK_TRUE;
    f->features.robustBufferAccess = VK_TRUE;
    f->features.independentBlend = VK_TRUE;
    f->features.fragmentStoresAndAtomics = VK_TRUE;
    f->features.vertexPipelineStoresAndAtomics = VK_TRUE;
    for (VkBaseOutStructure *n = f->pNext; n; n = n->pNext) {
        VkBool32 have = n->sType != missing_feature;
        switch (n->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: {
            assert(test_api >= VK_API_VERSION_1_2);
            VkPhysicalDeviceVulkan12Features *x = (void *)n;
            x->timelineSemaphore = have; x->samplerMirrorClampToEdge = have;
            x->shaderInt8 = VK_TRUE;  // enable chain must discard unused queried bits
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
            assert(test_api < VK_API_VERSION_1_2);
            ((VkPhysicalDeviceTimelineSemaphoreFeatures *)n)->timelineSemaphore = have; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES:
            ((VkPhysicalDeviceDynamicRenderingFeatures *)n)->dynamicRendering = have; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES:
            ((VkPhysicalDeviceSynchronization2Features *)n)->synchronization2 = have; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT:
            assert(test_api < VK_API_VERSION_1_3);
            ((VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *)n)->extendedDynamicState = have; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT:
            assert(test_api < VK_API_VERSION_1_3);
            ((VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *)n)->extendedDynamicState2 = have; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT:
            ((VkPhysicalDeviceCustomBorderColorFeaturesEXT *)n)->customBorderColors = optional_supported;
            ((VkPhysicalDeviceCustomBorderColorFeaturesEXT *)n)->customBorderColorWithoutFormat = optional_supported; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT:
            ((VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *)n)->vertexAttributeInstanceRateDivisor = optional_supported; break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT:
            ((VkPhysicalDeviceFaultFeaturesEXT *)n)->deviceFault = optional_supported; break;
        default: assert(!"unexpected feature query");
        }
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2 *p) {
    (void)pd;
    VkPhysicalDeviceSubgroupProperties *s = p->pNext;
    s->supportedStages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    s->supportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
                             (bad_subgroups ? 0 : VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pd, uint32_t *n,
                                                                   VkQueueFamilyProperties *q) {
    (void)pd;
    *n = 1;
    if (q) { memset(q, 0, sizeof *q); q->queueCount = 1; q->queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT; }
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice pd, uint32_t n, VkSurfaceKHR surface,
                                                                    VkBool32 *present) {
    (void)pd; (void)n; (void)surface; *present = VK_TRUE; return VK_SUCCESS;
}

static void fake_command(void) {}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice dev, const char *name) {
    (void)dev;
    bool found = false;
#define CHECK_COMMAND(command, suffix, version) \
    if (!strcmp(name, g_vk.api_version >= version ? #command : #command #suffix)) found = true;
    VK_ANDROID_COMMANDS(CHECK_COMMAND)
#undef CHECK_COMMAND
    assert(found);
    loaded++;
    return fake_command;
}

static bool enabled_extension(const VkAndroidCaps *c, const char *name) {
    for (uint32_t i = 0; i < c->n_ext; i++) if (!strcmp(c->ext[i], name)) return true;
    return false;
}

int main(void) {
    VkAndroidCaps c;
    assert(!android_capabilities(VK_NULL_HANDLE, VK_API_VERSION_1_0, &c));
    const uint32_t versions[] = {VK_API_VERSION_1_1, VK_API_VERSION_1_2, VK_API_VERSION_1_3};
    for (size_t i = 0; i < sizeof versions / sizeof versions[0]; i++) {
        test_api = versions[i];
        assert(android_capabilities(VK_NULL_HANDLE, test_api, &c));
        assert(!c.custom_border && !c.divisor && !c.fault); // advertised, but feature false
        assert(!enabled_extension(&c, VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME));
        assert(enabled_extension(&c, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == (test_api < VK_API_VERSION_1_2));
        assert(enabled_extension(&c, VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME) == (test_api < VK_API_VERSION_1_2));
        assert(enabled_extension(&c, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == (test_api < VK_API_VERSION_1_3));
        android_enable_features(&c);
        assert(!c.f12.shaderInt8);
        unsigned count = 0;
        for (VkBaseOutStructure *n = c.features.pNext; n; n = n->pNext) assert(++count <= 8);
        assert(android_present_queue(VK_NULL_HANDLE) == 0);
        g_vk.api_version = test_api;
        loaded = 0;
        android_load_commands();
        assert(loaded == 17);
    }
    test_api = VK_API_VERSION_1_1;
    missing_extension = VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME;
    assert(!android_capabilities(VK_NULL_HANDLE, test_api, &c));
    missing_extension = NULL;
    missing_feature = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    assert(!android_capabilities(VK_NULL_HANDLE, test_api, &c));
    missing_feature = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    assert(!android_capabilities(VK_NULL_HANDLE, test_api, &c));
    missing_feature = 0;
    bad_subgroups = true;
    assert(!android_capabilities(VK_NULL_HANDLE, test_api, &c));
    bad_subgroups = false;
    optional_supported = true;
    assert(android_capabilities(VK_NULL_HANDLE, test_api, &c));
    assert(c.custom_border && c.divisor && c.fault);
    android_enable_features(&c);
    assert(enabled_extension(&c, VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME));
    puts("Android Vulkan capability and dispatch tests passed");
    return 0;
}

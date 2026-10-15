// Android capability contract. Included by device.c after has_ext; no platform or loader dependencies.
// A lower API version is accepted only when every command/feature the renderer needs has an equivalent
// promoted extension. Never pass Vulkan12/13 aggregate feature structs to a Vulkan 1.1 device.
#pragma once

typedef struct {
    const char *ext[24];
    uint32_t n_ext, api;
    bool custom_border, divisor, host_import, fault;
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceVulkan12Features f12;
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline;
    VkPhysicalDeviceDynamicRenderingFeatures rendering;
    VkPhysicalDeviceSynchronization2Features sync;
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic;
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT dynamic2;
    VkPhysicalDeviceCustomBorderColorFeaturesEXT border;
    VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT div;
    VkPhysicalDeviceFaultFeaturesEXT faults;
} VkAndroidCaps;

static void android_chain(VkAndroidCaps *c, void *feature) {
    VkBaseOutStructure *node = feature;
    node->pNext = c->features.pNext;
    c->features.pNext = node;
}

static bool android_extension(VkAndroidCaps *c, const VkExtensionProperties *ep, uint32_t ne,
                              const char *name, uint32_t core) {
    if (core && c->api >= core) return true;
    if (!has_ext((VkExtensionProperties *)ep, ne, name)) {
        LOG("[vk] missing required extension: %s\n", name);
        return false;
    }
    c->ext[c->n_ext++] = name;
    return true;
}

static bool android_capabilities(VkPhysicalDevice pd, uint32_t api, VkAndroidCaps *c) {
    memset(c, 0, sizeof *c);
    c->api = api;
    if (api < VK_API_VERSION_1_1) { LOG("[vk] Vulkan 1.1 or newer is required\n"); return false; }
    uint32_t ne = 0;
    if (vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, NULL) != VK_SUCCESS) return false;
    VkExtensionProperties *ep = calloc(ne, sizeof *ep);
    if (!ep) return false;
    if (vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, ep) != VK_SUCCESS) { free(ep); return false; }
    bool ok = true;
#define REQUIRE_EXT(name, core) do { if (!android_extension(c, ep, ne, name, core)) ok = false; } while (0)
    REQUIRE_EXT(VK_KHR_SWAPCHAIN_EXTENSION_NAME, 0);
    REQUIRE_EXT(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME, VK_API_VERSION_1_2);
    REQUIRE_EXT(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME, VK_API_VERSION_1_2);
    // Dynamic rendering's Vulkan 1.1 dependency closure (depth/stencil resolve depends on renderpass2).
    REQUIRE_EXT(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME, VK_API_VERSION_1_2);
    REQUIRE_EXT(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME, VK_API_VERSION_1_2);
    REQUIRE_EXT(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_API_VERSION_1_3);
    REQUIRE_EXT(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, VK_API_VERSION_1_3);
    REQUIRE_EXT(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME, VK_API_VERSION_1_3);
    REQUIRE_EXT(VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME, VK_API_VERSION_1_3);
    // The Vulkan 1.1 shader target represents local_size_*_id using a specialized WorkgroupSize constant,
    // rather than the LocalSizeId execution mode of the desktop target, so maintenance4 is unnecessary here.
#undef REQUIRE_EXT
    if (!ok) { free(ep); return false; }

    c->features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    if (api >= VK_API_VERSION_1_2) {
        c->f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        android_chain(c, &c->f12);
    } else {
        c->timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        android_chain(c, &c->timeline);
    }
    c->rendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    c->sync.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    android_chain(c, &c->rendering);
    android_chain(c, &c->sync);
    if (api < VK_API_VERSION_1_3) {
        c->dynamic.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        c->dynamic2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
        android_chain(c, &c->dynamic);
        android_chain(c, &c->dynamic2);
    }
    if (has_ext(ep, ne, VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME)) {
        c->border.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
        android_chain(c, &c->border);
    }
    if (has_ext(ep, ne, VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME)) {
        c->div.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT;
        android_chain(c, &c->div);
    }
    if (has_ext(ep, ne, VK_EXT_DEVICE_FAULT_EXTENSION_NAME)) {
        c->faults.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
        android_chain(c, &c->faults);
    }
    vkGetPhysicalDeviceFeatures2(pd, &c->features);
#define REQUIRE_FEATURE(value, name) do { if (!(value)) { LOG("[vk] missing required feature: %s\n", name); ok = false; } } while (0)
    REQUIRE_FEATURE(api >= VK_API_VERSION_1_2 ? c->f12.timelineSemaphore : c->timeline.timelineSemaphore, "timelineSemaphore");
    if (api >= VK_API_VERSION_1_2) REQUIRE_FEATURE(c->f12.samplerMirrorClampToEdge, "samplerMirrorClampToEdge");
    REQUIRE_FEATURE(c->rendering.dynamicRendering, "dynamicRendering");
    REQUIRE_FEATURE(c->sync.synchronization2, "synchronization2");
    if (api < VK_API_VERSION_1_3) {
        REQUIRE_FEATURE(c->dynamic.extendedDynamicState, "extendedDynamicState");
        REQUIRE_FEATURE(c->dynamic2.extendedDynamicState2, "extendedDynamicState2");
    }
    REQUIRE_FEATURE(c->features.features.shaderInt64, "shaderInt64 (guest address arithmetic)");
    REQUIRE_FEATURE(c->features.features.robustBufferAccess, "robustBufferAccess (guest constant buffers)");
    REQUIRE_FEATURE(c->features.features.independentBlend, "independentBlend");
    REQUIRE_FEATURE(c->features.features.fragmentStoresAndAtomics, "fragmentStoresAndAtomics");
    REQUIRE_FEATURE(c->features.features.vertexPipelineStoresAndAtomics, "vertexPipelineStoresAndAtomics");
    VkPhysicalDeviceSubgroupProperties subgroup = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &subgroup};
    vkGetPhysicalDeviceProperties2(pd, &properties);
    VkSubgroupFeatureFlags operations = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
                                       VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT;
    REQUIRE_FEATURE((subgroup.supportedOperations & operations) == operations, "subgroup shuffle and relative shuffle");
    REQUIRE_FEATURE((subgroup.supportedStages & (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)) ==
                    (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT), "fragment and compute subgroups");
#undef REQUIRE_FEATURE

    // Optional features must be queried too: extension advertisement alone doesn't enable its feature bits.
    c->custom_border = c->border.customBorderColors && c->border.customBorderColorWithoutFormat;
    c->divisor = c->div.vertexAttributeInstanceRateDivisor;
    c->host_import = has_ext(ep, ne, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    c->fault = c->faults.deviceFault;
    if (c->custom_border) c->ext[c->n_ext++] = VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME;
    if (c->divisor) c->ext[c->n_ext++] = VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME;
    if (c->host_import) c->ext[c->n_ext++] = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    if (c->fault) c->ext[c->n_ext++] = VK_EXT_DEVICE_FAULT_EXTENSION_NAME;
    free(ep);
    return ok;
}

// Rebuild the enable chain from the contract, rather than enabling every feature returned by a query.
static void android_enable_features(VkAndroidCaps *c) {
    c->features.pNext = NULL;
    if (c->api >= VK_API_VERSION_1_2) {
        c->f12 = (VkPhysicalDeviceVulkan12Features){.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .timelineSemaphore = VK_TRUE, .samplerMirrorClampToEdge = VK_TRUE};
        android_chain(c, &c->f12);
    } else android_chain(c, &c->timeline);
    android_chain(c, &c->rendering);
    android_chain(c, &c->sync);
    if (c->api < VK_API_VERSION_1_3) {
        c->dynamic2.extendedDynamicState2LogicOp = VK_FALSE;
        c->dynamic2.extendedDynamicState2PatchControlPoints = VK_FALSE;
        android_chain(c, &c->dynamic);
        android_chain(c, &c->dynamic2);
    }
    if (c->custom_border) android_chain(c, &c->border);
    if (c->divisor) {
        c->div.vertexAttributeInstanceRateZeroDivisor = VK_FALSE;
        android_chain(c, &c->div);
    }
    if (c->fault) {
        c->faults.deviceFaultVendorBinary = VK_FALSE;
        android_chain(c, &c->faults);
    }
}

static uint32_t android_present_queue(VkPhysicalDevice pd) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, NULL);
    VkQueueFamilyProperties *q = calloc(count, sizeof *q);
    if (!q) return UINT32_MAX;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, q);
    uint32_t result = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        VkBool32 present = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, g_vk.surface, &present) == VK_SUCCESS && present &&
            q[i].queueCount && (q[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                                  (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) { result = i; break; }
    }
    free(q);
    return result;
}

static void android_load_commands(void) {
#define LOAD_ANDROID_COMMAND(name, suffix, version) do { \
    g_vk.p_##name = (PFN_##name)vkGetDeviceProcAddr(g_vk.dev, g_vk.api_version >= version ? #name : #name #suffix); \
    if (!g_vk.p_##name) FATAL("[vk] required device command unavailable: %s%s\n", #name, g_vk.api_version >= version ? "" : #suffix); \
} while (0);
    VK_ANDROID_COMMANDS(LOAD_ANDROID_COMMAND)
#undef LOAD_ANDROID_COMMAND
}

// Host Vulkan-driver smoke through volk; compile volk.c and link -ldl, never -lvulkan.
// Set VK_ICD_FILENAMES to a software ICD; no game input is required.
// Exercises real device creation, all promoted command pointers and timeline semaphore calls at each API.
#define __ANDROID__ 1
#define LARECOMP_ANDROID_CUSTOM_VULKAN 1
#include <stdlib.h>
#include <string.h>
#include "../vk.h"
VkG g_vk;
void rt_fatal_trace(void) { abort(); }
static bool has_ext(VkExtensionProperties *e, uint32_t n, const char *name) {
    for (uint32_t i = 0; i < n; i++) if (!strcmp(e[i].extensionName, name)) return true;
    return false;
}
#include "../android_caps.h"
int main(void) {
    if (volkInitialize() != VK_SUCCESS) return 10;
    const uint32_t versions[] = {VK_API_VERSION_1_1, VK_API_VERSION_1_2, VK_API_VERSION_1_3};
    for (size_t v = 0; v < sizeof versions / sizeof versions[0]; v++) {
        VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = versions[v]};
        const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app,
            .enabledExtensionCount = 1, .ppEnabledExtensionNames = extensions};
        if (vkCreateInstance(&ic, NULL, &g_vk.inst) != VK_SUCCESS) return 1;
        volkLoadInstanceOnly(g_vk.inst);
        uint32_t n = 1;
        if (vkEnumeratePhysicalDevices(g_vk.inst, &n, &g_vk.phys) != VK_SUCCESS) return 2;
        VkAndroidCaps caps;
        if (!android_capabilities(g_vk.phys, versions[v], &caps)) return 3;
        android_enable_features(&caps);
        float priority = 1;
        VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1,
            .pQueuePriorities = &priority};
        VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &caps.features,
            .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi, .enabledExtensionCount = caps.n_ext,
            .ppEnabledExtensionNames = caps.ext};
        if (vkCreateDevice(g_vk.phys, &di, NULL, &g_vk.dev) != VK_SUCCESS) return 4;
        volkLoadDevice(g_vk.dev);
        g_vk.api_version = versions[v];
        android_load_commands();
        VkSemaphoreTypeCreateInfo type = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE, .initialValue = 42};
        VkSemaphoreCreateInfo sc = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &type};
        VkSemaphore sem;
        if (vkCreateSemaphore(g_vk.dev, &sc, NULL, &sem) != VK_SUCCESS) return 5;
        uint64_t value = 0;
        if (vkGetSemaphoreCounterValue(g_vk.dev, sem, &value) != VK_SUCCESS || value != 42) return 6;
        vkDestroySemaphore(g_vk.dev, sem, NULL);
        vkDestroyDevice(g_vk.dev, NULL);
        vkDestroyInstance(g_vk.inst, NULL);
        printf("Real driver capability/device/dispatch/timeline passed for Vulkan1.%u\n", VK_API_VERSION_MINOR(versions[v]));
    }
    return 0;
}

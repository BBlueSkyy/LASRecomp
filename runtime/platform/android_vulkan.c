// Complete Vulkan dispatch originates here when LARECOMP_ANDROID_CUSTOM_VULKAN is enabled.
// libadrenotools provides Android's isolated-loader/WSI integration for rootless per-app Turnip.
#include "android_vulkan.h"
#include <adrenotools/driver.h>
#include <volk.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *provider_handle;
static bool custom, initialized;
bool android_vulkan_is_custom(void) { return custom; }

bool android_vulkan_init(char *error, size_t error_size) {
    if (initialized) return true;
    if (provider_handle) {
        snprintf(error, error_size, "Vulkan provider initialization failed; restart the process");
        return false;
    }
    const char *selection = getenv("LARECOMP_VK_DRIVER");
    if (!selection || !strcmp(selection, "system")) {
        provider_handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (!provider_handle) {
            snprintf(error, error_size, "system Vulkan loader: %s", dlerror());
            return false;
        }
    } else if (!strcmp(selection, "custom")) {
        const char *dir = getenv("LARECOMP_VK_DRIVER_DIR");
        const char *name = getenv("LARECOMP_VK_DRIVER_NAME");
        const char *hooks = getenv("LARECOMP_ANDROID_NATIVE_LIB_DIR");
        const char *temp = getenv("LARECOMP_VK_TMP_DIR");
        if (!dir || dir[0] != '/' || !name || !*name || strchr(name, '/') || strstr(name, "..") ||
            !hooks || hooks[0] != '/' || !temp || temp[0] != '/') {
            snprintf(error, error_size, "custom Vulkan requires an app-private driver directory, driver basename, nativeLibraryDir and cache directory");
            return false;
        }
        // Upstream concatenates the directory and basename without inserting a separator.
        char driver_dir[4096];
        if (snprintf(driver_dir, sizeof driver_dir, "%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/") >= (int)sizeof driver_dir) {
            snprintf(error, error_size, "custom Vulkan driver path too long");
            return false;
        }
        provider_handle = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM,
            temp, hooks, driver_dir, name, NULL, NULL);
        if (!provider_handle) {
            snprintf(error, error_size, "libadrenotools could not load the selected driver; verify arm64 compatibility and packaged hooks");
            return false;
        }
        custom = true;
    } else {
        snprintf(error, error_size, "unknown Vulkan provider '%s'", selection);
        return false;
    }
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(provider_handle, "vkGetInstanceProcAddr");
    if (!get) {
        snprintf(error, error_size, "selected Vulkan loader has no vkGetInstanceProcAddr");
        return false;
    }
    volkInitializeCustom(get);
    if (!vkCreateInstance || !vkEnumerateInstanceExtensionProperties) {
        snprintf(error, error_size, "selected Vulkan loader has incomplete global dispatch");
        return false;
    }
    initialized = true;
    return true;
}

// Android process-wide Vulkan provider. Selected driver remains loaded until process exit.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
bool android_vulkan_init(char *error, size_t error_size);
bool android_vulkan_is_custom(void);
#ifdef __cplusplus
}
#endif

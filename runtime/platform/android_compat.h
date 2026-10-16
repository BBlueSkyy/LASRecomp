// Bionic compatibility. Do not include glibc-specific linux_compat.h on Android.
// Bionic provides real strlcpy/strlcat, arc4random and pthread/time APIs at the
// project's API 28 minimum; no replacements or fake synchronization are needed.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
// The standalone platform library loads before the runtime to claim the guest
// window. Call once from the runtime entry, before starting threads/installing
// crash handlers, with the real rt_fault_watch/report and rt_host_fn_name.
void plat_android_set_runtime_hooks(bool (*watch)(uint64_t, uint64_t),
                                    void (*report)(const char *, uint64_t, uint64_t, uint64_t),
                                    const char *(*symbol)(uint64_t, uint64_t *));
#ifdef __cplusplus
}
#endif

#include "paths.h"
#include "cpu.h"
bool rt_resource(const char *name, const char *dev_rel, char *out, size_t size) {
    (void)name; (void)dev_rel; (void)out; (void)size;
    return false;
}
uint64_t rt_ticks(void) { return 42; }

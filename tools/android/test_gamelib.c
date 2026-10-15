#define _POSIX_C_SOURCE 200809L
#include "gamelib_validate.h"
#include <assert.h>
#include <dlfcn.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const void *p; size_t n; bool code; } Region;
static Region regions[8];
static size_t nregions;
static bool checked_range(const void *ptr, size_t n, bool code, void *unused) {
    (void)unused;
    uintptr_t a = (uintptr_t)ptr;
    for (size_t i = 0; i < nregions; ++i) {
        uintptr_t lo = (uintptr_t)regions[i].p;
        if (code == regions[i].code && a >= lo && a - lo <= regions[i].n &&
            n <= regions[i].n - (a - lo)) return true;
    }
    return false;
}
static void allow(const void *p, size_t n, bool code) { regions[nregions++] = (Region){p, n, code}; }
static void guest(Cpu *c) { c->x[0] = 1; }

static void unit_tables(void) {
    uint64_t main[] = {0x7000000000ull, 0x7000000004ull};
    uint64_t sdk[] = {0x7004000000ull};
    uint64_t nm = 2, ns = 1;
    GuestFn fns[] = {guest, guest};
    const char id[] = GAMELIB_ID;
    GameLibTables t = {GAMELIB_ABI, id, main, sdk, fns, fns, &nm, &ns};
    allow(&t, sizeof t, false); allow(main, sizeof main, false); allow(sdk, sizeof sdk, false);
    allow(&nm, sizeof nm, false); allow(&ns, sizeof ns, false); allow(fns, sizeof fns, false);
    allow(id, sizeof id, false); allow((void *)(uintptr_t)guest, 1, true);
    char why[256];
    assert(gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    nm = 0;
    assert(!gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    nm = UINT64_MAX;
    assert(!gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    nm = 2;
    main[1] = main[0];
    assert(!gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    main[1] = main[0] + 1;
    assert(!gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    main[1] = 0x7004000000ull;
    assert(!gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    main[1] = main[0] + 4;
    fns[1] = (GuestFn)(uintptr_t)main;
    assert(!gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    fns[1] = guest;
    t.id = (const char *)1;
    assert(!gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    t.id = id; t.main_count = (const uint64_t *)1;
    assert(!gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    t.main_count = &nm; t.abi++;
    assert(!gamelib_validate_tables(&t, checked_range, NULL, why, sizeof why));
    assert(!gamelib_validate_tables((const GameLibTables *)1, checked_range, NULL, why, sizeof why));
}

int main(int argc, char **argv) {
    assert(argc >= 2);
    unit_tables();
    char why[256];
    unsigned machine;
#if defined(__aarch64__)
    machine = EM_AARCH64;
#elif defined(__x86_64__)
    machine = EM_X86_64;
#else
#error Test supports ARM64 and x86_64 hosts
#endif
    if (!strcmp(argv[1], "elf")) {
        assert(argc == 4);
        bool valid = gamelib_validate_elf(argv[2], machine, why, sizeof why);
        if (valid != (atoi(argv[3]) != 0)) { fprintf(stderr, "unexpected ELF result: %s\n", why); return 1; }
        return 0;
    }
    assert(argc == 5 && !strcmp(argv[1], "load"));
    void *runtime = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
    if (!runtime) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    bool (*load)(const char *, char *, size_t) = dlsym(runtime, "gamelib_load");
    const GameLibTables *(*tables)(void) = dlsym(runtime, "gamelib_tables");
    assert(load && tables);
    setenv("LARECOMP_GAME_LIB", argv[3], 1);
    assert(!load(NULL, why, sizeof why));
    assert(!tables());
    setenv("LARECOMP_GAME_LIB", argv[4], 1);
    if (!load(NULL, why, sizeof why)) { fprintf(stderr, "%s\n", why); return 1; }
    Cpu cpu = {0};
    tables()->main_fns[0](&cpu);
    assert(cpu.x[0] == 42);  // resolved import came from the shared runtime
    assert(load(NULL, why, sizeof why));
    puts("ELF, pointer validation, imported runtime call, and failed-load retry passed");
    return 0;
}

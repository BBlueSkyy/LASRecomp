// Loads the recompiled game code library (runtime/gamelib.h).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "gamelib.h"
#include "gamelib_validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#if defined(__linux__) || defined(__ANDROID__)
#include <elf.h>
#include <link.h>
#endif

#include "paths.h"
#include "rt.h"

static const GameLibTables *g_tables;
static char g_error[256];

#if defined(__linux__) || defined(__ANDROID__)
typedef struct {
    uintptr_t entry;
    struct { uintptr_t lo, hi; unsigned flags; } segments[64];
    size_t n;
} ModuleRanges;

static int module_ranges(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    ModuleRanges *r = data;
    bool ours = false;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        uintptr_t lo = info->dlpi_addr + p->p_vaddr;
        if (p->p_type == PT_LOAD && (p->p_flags & PF_X) && r->entry >= lo && r->entry - lo < p->p_memsz)
            ours = true;
    }
    if (!ours) return 0;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_LOAD) continue;
        if (r->n == sizeof r->segments / sizeof r->segments[0] ||
            p->p_vaddr > UINTPTR_MAX - info->dlpi_addr ||
            p->p_memsz > UINTPTR_MAX - (info->dlpi_addr + p->p_vaddr)) {
            r->n = 0; return 1;
        }
        r->segments[r->n].lo = info->dlpi_addr + p->p_vaddr;
        r->segments[r->n].hi = info->dlpi_addr + p->p_vaddr + p->p_memsz;
        r->segments[r->n++].flags = p->p_flags;
    }
    return 1;
}

static bool mapped_range(const void *ptr, size_t size, bool code, void *context) {
    const ModuleRanges *r = context;
    uintptr_t address = (uintptr_t)ptr;
    for (size_t i = 0; i < r->n; ++i) {
        if ((r->segments[i].flags & (code ? PF_X : PF_R)) &&
            address >= r->segments[i].lo && address <= r->segments[i].hi &&
            size <= r->segments[i].hi - address) return true;
    }
    return false;
}
#endif

// The library's id file (<library>.id, written next to it by the build): its GAMELIB_ID, read without loading it.
static bool id_matches(const char *lib) {
    char path[1100], id[128] = {0};
    snprintf(path, sizeof path, "%s.id", lib);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(id, 1, sizeof id - 1, f);
    fclose(f);
    while (n && (id[n - 1] == '\n' || id[n - 1] == '\r')) id[--n] = 0;
    return !strcmp(id, GAMELIB_ID);
}

static bool exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// The places to look, in order (gamelib.h). Returns how many were filled.
static int candidates(const char *game_dir, char out[3][1100]) {
    int n = 0;
    const char *env = getenv("LARECOMP_GAME_LIB");
    if (env && *env) snprintf(out[n++], 1100, "%s", env);
    char exe_side[1024];
    if (rt_resource(GAMELIB_NAME, NULL, exe_side, sizeof exe_side) && exists(exe_side))
        snprintf(out[n++], 1100, "%s", exe_side);
    if (game_dir) snprintf(out[n++], 1100, "%s/%s", game_dir, GAMELIB_NAME);
    return n;
}

bool gamelib_present(const char *game_dir) {
    if (g_tables) return true;
    char c[3][1100];
    int n = candidates(game_dir, c);
    for (int i = 0; i < n; i++)
        if (exists(c[i]) && id_matches(c[i])) return true;
    return false;
}

static const GameLibTables *open_lib(const char *path) {
#ifdef __ANDROID__
    // SDL loads liblasrecomp through JNI with local visibility on some Android
    // versions. Generated code imports its runtime services; promote the actual
    // loaded library instead of depending on the process executable's exports.
    // Keep this handle for the process lifetime, just like the game module.
    static void *runtime_global;
    if (!runtime_global) {
        Dl_info info;
        if (!dladdr((const void *)(uintptr_t)gamelib_load, &info) || !info.dli_fname ||
            !(runtime_global = dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD))) {
            const char *error = dlerror();
            snprintf(g_error, sizeof g_error, "cannot expose the Android runtime: %s", error ? error : "unknown library");
            return NULL;
        }
    }
    if (!gamelib_validate_elf(path, EM_AARCH64, g_error, sizeof g_error)) return NULL;
#endif
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    if (!h) { snprintf(g_error, sizeof g_error, "can't load %s (error %lu)", path, GetLastError()); return NULL; }
    GameLibEntry entry = (GameLibEntry)(void *)GetProcAddress(h, GAMELIB_ENTRY);
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { snprintf(g_error, sizeof g_error, "%s", dlerror()); return NULL; }
    GameLibEntry entry = (GameLibEntry)dlsym(h, GAMELIB_ENTRY);
#endif
    if (!entry) {
        snprintf(g_error, sizeof g_error, "%s: not game code", path);
        goto failed;
    }
#if defined(__linux__) || defined(__ANDROID__)
    ModuleRanges ranges = {.entry = (uintptr_t)entry};
    dl_iterate_phdr(module_ranges, &ranges);
    if (!mapped_range((const void *)(uintptr_t)entry, 1, true, &ranges)) {
        snprintf(g_error, sizeof g_error, "game module entry is not executable code");
        goto failed;
    }
#endif
    const GameLibTables *t = entry();
#if defined(__linux__) || defined(__ANDROID__)
    if (!gamelib_validate_tables(t, mapped_range, &ranges, g_error, sizeof g_error)) goto failed;
#else
    if (!t || t->abi != GAMELIB_ABI || !t->id || strcmp(t->id, GAMELIB_ID)) {
        snprintf(g_error, sizeof g_error, "%s was built for another version", path);
        goto failed;
    }
#endif
    return t;
failed:
#ifdef _WIN32
    FreeLibrary(h);
#else
    dlclose(h);
#endif
    return NULL;
}

bool gamelib_load(const char *game_dir, char *why, size_t why_size) {
    if (!g_tables) {
        snprintf(g_error, sizeof g_error, "the game code hasn't been built: set up the game files on the start screen");
        char c[3][1100];
        int n = candidates(game_dir, c);
        const char *env = getenv("LARECOMP_GAME_LIB");
        for (int i = 0; i < n && !g_tables; i++) {
            if (!exists(c[i])) continue;
            bool given = i == 0 && env && *env;  // LARECOMP_GAME_LIB: loaded as given, the library's own id decides
            if (!given && !id_matches(c[i])) {
                snprintf(g_error, sizeof g_error, "%s was built for another version", c[i]);
                continue;
            }
            g_tables = open_lib(c[i]);
            if (g_tables) LOG("[gamelib] %s (%llu + %llu functions)\n", c[i], (unsigned long long)*g_tables->main_count,
                              (unsigned long long)*g_tables->sdk_count);
        }
    }
    if (!g_tables && why) snprintf(why, why_size, "%s", g_error);
    return g_tables != NULL;
}

const GameLibTables *gamelib_tables(void) { return g_tables; }

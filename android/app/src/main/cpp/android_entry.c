// Real SDL Android entry. No guest code or synthetic frame is substituted for a missing game library.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "gamelib.h"
#include "platform.h"
#include "platform/android_prepare.h"
#include "rt.h"

int las_runtime_main(int argc, char **argv);
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset);

int SDL_main(int argc, char **argv) {
    const char *data = SDL_GetAndroidInternalStoragePath();
    if (!data || setenv("LARECOMP_DATA_ROOT", data, 1) != 0) return 1;
    char log[1200];
    snprintf(log, sizeof log, "%s/lasrecomp.log", data);
    if (freopen(log, "w", stderr)) dup2(fileno(stderr), STDOUT_FILENO);
    setvbuf(stderr, NULL, _IOLBF, 0);
    plat_android_set_runtime_hooks(rt_fault_watch, rt_fault_report, rt_host_fn_name);
    plat_init();
    if (!plat_reserve_guest(GUEST_LO, GUEST_HI)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LAS Recomp",
            "The original guest address range overlaps Android mappings. Details are in lasrecomp.log. "
            "No Android mapping was replaced. Address translation is still required on this layout.", NULL);
        return 2;
    }
    const char *root = argc > 1 ? argv[1] : "extracted";
    char why[1024];
    if (!gamelib_load(root, why, sizeof why)) {
        fprintf(stderr, "[android] cannot start guest: %s\n", why);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LAS Recomp — game code unavailable",
            "The Android runtime is linked, but on-device static generation and compilation are not bundled yet. "
            "Importing game files alone cannot boot this build. Details are in lasrecomp.log.", NULL);
        return 3;
    }
    if (!android_prepare_game(root, why, sizeof why)) {
        fprintf(stderr, "[android] game preparation failed: %s\n", why);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LAS Recomp", why, NULL);
        return 4;
    }
    return las_runtime_main(argc, argv);
}

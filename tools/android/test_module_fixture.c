// Synthetic code only: no game assets or generated proprietary code.
#include "gamelib.h"
#include "address_layout.h"
#ifndef BAD_KIND
#define BAD_KIND 0
#endif
#if BAD_KIND == 8
extern uint64_t nonexistent_runtime_import(void);
static void guest(Cpu *c) { c->x[0] = nonexistent_runtime_import(); }
#else
static void guest(Cpu *c) { c->x[0] = rt_ticks(); }
#endif
GAMELIB_EXPORT const GameLibTables *lasgame_tables(void) {
    static const uint64_t main[] = {MAIN_BASE, MAIN_BASE + 4};
    static const uint64_t unsorted[] = {MAIN_BASE + 4, MAIN_BASE};
    static const uint64_t sdk[] = {SDK_BASE};
    static const uint64_t count = 2, one = 1, zero = 0, huge = UINT64_MAX;
    static const GuestFn fns[] = {guest, guest};
    static const GuestFn bad_fns[] = {(GuestFn)(uintptr_t)1, guest};
    static const GameLibTables table = {
        BAD_KIND == 1 ? GAMELIB_ABI + 1 : GAMELIB_ABI,
        BAD_KIND == 2 ? "wrong-version" : GAMELIB_ID,
        BAD_KIND == 4 ? unsorted : main, sdk,
        BAD_KIND == 5 ? bad_fns : fns, fns,
        BAD_KIND == 3 ? &zero : BAD_KIND == 6 ? &huge : &count, &one,
    };
    if (BAD_KIND == 7) return (const GameLibTables *)(uintptr_t)1;
    return &table;
}

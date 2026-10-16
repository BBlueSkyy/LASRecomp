// Loaded by GameActivity before SDL and the large recompiled game library.
// Ownership and the platform implementation live in this same shared library.
#include "../rt.h"

__attribute__((constructor(101))) static void reserve_android_guest_early(void) {
    // A collision is recoverable here: SDL_main retries the same exact window
    // and can present the platform's detailed failure to the user. Never exit
    // or unmap another component's VMAs from a Java library-load constructor.
    (void)plat_reserve_guest(GUEST_LO, GUEST_HI);
}

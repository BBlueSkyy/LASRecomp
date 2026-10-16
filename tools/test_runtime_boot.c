// Synthetic NSOs and compiled guest functions: no extracted game data required.
#define _GNU_SOURCE
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include "../runtime/gamelib.h"
#include "../runtime/hle.h"
#include "../runtime/kernel.h"
#include "../runtime/platform/android_compat.h"

static _Atomic int completed;
static pthread_mutex_t completed_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t completed_changed = PTHREAD_COND_INITIALIZER;
static int empty_sdk;
static int fatal_guest;
static void compiled_sdk(Cpu *c) { c->x[0] += 7; }
HLE_FUNC("fixture_hle", fixture_hle) { c->x[0] += 11; }
static void compiled_main(Cpu *c) {
    if (fatal_guest == 1) rt_brk(c, 0, MAIN_BASE + 0x80);
    if (fatal_guest == 2) raise(SIGILL);
    assert(c->thread == kernel_current_thread());
    assert(c->tpidrro >= TLS_BASE && c->tpidrro + 0x200 <= TLS_BASE + TLS_SIZE);
    assert(c->sp >= STACK_BASE && c->sp <= STACK_BASE + STACK_SIZE);
    assert(*(uint64_t *)c->tpidrro == 0);
    *(uint64_t *)c->tpidrro = 0xface;
    c->x[1] = 0x200000;
    rt_svc(c, 1, MAIN_BASE + 0x80);
    assert(c->x[0] == 0 && c->x[1] == HEAP_BASE);
    *(uint64_t *)HEAP_BASE = 0xbeef;
    c->x[0] = 1;
    guest_call(c, *(uint64_t *)(MAIN_BASE + 0x2000));
    assert(c->x[0] == 8);
    guest_call(c, *(uint64_t *)(MAIN_BASE + 0x2008));
    assert(c->x[0] == 19);
    assert(*(uint64_t *)(MAIN_BASE + 0x2010) == 0);
    assert(*(uint64_t *)(MAIN_BASE + 0x2018) == MAIN_BASE + 0x80);
    pthread_mutex_lock(&completed_lock);
    atomic_fetch_add(&completed, 1);
    pthread_cond_signal(&completed_changed);
    pthread_mutex_unlock(&completed_lock);
}
static const uint64_t main_addrs[] = {MAIN_BASE + 0x80}, sdk_addrs[] = {SDK_BASE + 0x80};
static const GuestFn main_fns[] = {compiled_main}, sdk_fns[] = {compiled_sdk};
static const uint64_t one = 1, zero = 0;
const GameLibTables *gamelib_tables(void) {
    static GameLibTables t;
    t = (GameLibTables){GAMELIB_ABI, "fixture", main_addrs, empty_sdk ? NULL : sdk_addrs,
                       main_fns, empty_sdk ? NULL : sdk_fns, &one, empty_sdk ? &zero : &one};
    return &t;
}
// Services aren't needed by the fixture; keep the real SVC/thread implementation.
uint32_t ipc_connect_named_port(const char *n) { (void)n; abort(); }
int ipc_handle_request(Cpu *c, uint32_t h) { (void)c; (void)h; abort(); }
uint32_t vi_vsync_handle(void) { return 0; }

int main(int argc, char **argv) {
    assert(argc == 3);
    // These death tests deliberately abort with a 12.5 GiB anonymous guest
    // window. The Ubuntu runner otherwise pipes that image to its core helper
    // (CoreDumping=1, wchan=anon_pipe_write) before waitpid can observe SIGABRT.
    // RLIMIT_CORE alone does not disable a pipe-based collector. Keep real fatal
    // signals/handlers; disable only core collection in this fixture process.
    struct rlimit no_core = {0, 0};
    assert(setrlimit(RLIMIT_CORE, &no_core) == 0);
    assert(prctl(PR_SET_DUMPABLE, 0) == 0);
    assert(prctl(PR_GET_DUMPABLE) == 0);
    plat_android_set_runtime_hooks(rt_fault_watch, rt_fault_report, NULL);
    assert(plat_reserve_guest(GUEST_LO, GUEST_HI));
    if (!strcmp(argv[2], "empty-table")) {
        empty_sdk = 1;
        dispatch_init();
        assert(dispatch_lookup(SDK_BASE + 0x80) == NULL);
        assert(dispatch_lookup(MAIN_BASE + 0x80) == compiled_main);
        return 0;
    }
    loader_load(argv[1]);
    kernel_init();
    dispatch_init();
    if (!strcmp(argv[2], "bad-stack")) {
        kernel_create_main_thread(MAIN_BASE + 0x80, STACK_SIZE + 0x10000, 44);
        return 0;
    }
    if (!strcmp(argv[2], "zero-stack")) {
        kernel_create_main_thread(MAIN_BASE + 0x80, 0, 44);
        return 0;
    }
    if (!strcmp(argv[2], "fatal-guest")) fatal_guest = 1;
    if (!strcmp(argv[2], "signal-guest")) fatal_guest = 2;
    if (!strcmp(argv[2], "load-only")) return 0;
    for (int boot = 0; boot < 3; ++boot) {
        assert(g_nmodules == 2);
        assert(loader_find_symbol("fixture_sdk") == SDK_BASE + 0x80);
        KThread *t = kernel_create_main_thread(MAIN_BASE + 0x80, 0x100000, 44);
        kernel_run_main(t, t->handle, 0, 0, 0);
        pthread_mutex_lock(&completed_lock);
        while (atomic_load(&completed) != boot + 1) pthread_cond_wait(&completed_changed, &completed_lock);
        pthread_mutex_unlock(&completed_lock);
        assert(atomic_load(&completed) == boot + 1);
        kernel_stop_threads(NULL);
        kernel_reset();
        assert(plat_reset_guest(GUEST_LO, GUEST_HI));
        // Match production teardown, then load into the same Module records.
        g_nmodules = 0;
        loader_load(argv[1]);
        kernel_init();
    }
    return 0;
}

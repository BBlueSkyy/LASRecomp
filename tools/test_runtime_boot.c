// Synthetic NSOs and compiled guest functions: no extracted game data required.
#define _GNU_SOURCE
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/mman.h>
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
static int worker_completed;
static void compiled_worker(Cpu *c) {
    assert(c->x[0] == 0x1234);
    pthread_attr_t attr;
    void *stack;
    size_t size;
    assert(pthread_getattr_np(pthread_self(), &attr) == 0);
    assert(pthread_attr_getstack(&attr, &stack, &size) == 0);
    assert(size >= (256ull << 20));
    assert(pthread_attr_destroy(&attr) == 0);
    pthread_mutex_lock(&completed_lock);
    worker_completed++;
    pthread_cond_signal(&completed_changed);
    pthread_mutex_unlock(&completed_lock);
}
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
static const uint64_t main_addrs[] = {MAIN_BASE + 0x80, MAIN_BASE + 0x84}, sdk_addrs[] = {SDK_BASE + 0x80};
static const GuestFn main_fns[] = {compiled_main, compiled_worker}, sdk_fns[] = {compiled_sdk};
static const uint64_t one = 1, two = 2, zero = 0;
const GameLibTables *gamelib_tables(void) {
    static GameLibTables t;
    t = (GameLibTables){GAMELIB_ABI, "fixture", main_addrs, empty_sdk ? NULL : sdk_addrs,
                       main_fns, empty_sdk ? NULL : sdk_fns, &two, empty_sdk ? &zero : &one};
    return &t;
}
// Services aren't needed by the fixture; keep the real SVC/thread implementation.
uint32_t ipc_connect_named_port(const char *n) { (void)n; abort(); }
int ipc_handle_request(Cpu *c, uint32_t h) { (void)c; (void)h; abort(); }
uint32_t vi_vsync_handle(void) { return 0; }

// Force genuine pthread_create exhaustion without replacing any pthread call.
// Leave the hard limit untouched so the exact same KThread can be retried.
static struct rlimit restrict_next_host_stack(void) {
    FILE *statm = fopen("/proc/self/statm", "r");
    unsigned long pages;
    assert(statm && fscanf(statm, "%lu", &pages) == 1);
    assert(fclose(statm) == 0);
    struct rlimit saved;
    assert(getrlimit(RLIMIT_AS, &saved) == 0);
    struct rlimit limited = saved;
    limited.rlim_cur = (rlim_t)pages * (rlim_t)sysconf(_SC_PAGESIZE) + (64ull << 20);
    assert(saved.rlim_cur == RLIM_INFINITY || limited.rlim_cur < saved.rlim_cur);
    assert(setrlimit(RLIMIT_AS, &limited) == 0);
    return saved;
}

typedef struct {
    uint32_t handle, result;
    pthread_barrier_t *ready;
} StartAttempt;
static void *start_attempt(void *opaque) {
    StartAttempt *attempt = opaque;
    int rc = pthread_barrier_wait(attempt->ready);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    Cpu cpu = {0};
    cpu.x[0] = attempt->handle;
    rt_svc(&cpu, 0x09, MAIN_BASE + 0x80);
    attempt->result = (uint32_t)cpu.x[0];
    return NULL;
}

static void thread_resource_retry(void) {
    Cpu cpu = {0};
    cpu.x[1] = MAIN_BASE + 0x84;
    cpu.x[2] = 0x1234;
    cpu.x[3] = STACK_BASE + STACK_SIZE;
    cpu.x[4] = 44;
    rt_svc(&cpu, 0x08, MAIN_BASE + 0x80);
    assert(cpu.x[0] == 0);
    uint32_t handle = (uint32_t)cpu.x[1];
    KThread *thread = (KThread *)handle_get(handle);
    assert(thread && thread->obj.type == KOBJ_THREAD);
    struct rlimit saved = restrict_next_host_stack();
    cpu.x[0] = handle;
    rt_svc(&cpu, 0x09, MAIN_BASE + 0x80);
    uint64_t failure = cpu.x[0];
    assert(setrlimit(RLIMIT_AS, &saved) == 0);
    assert(failure == 0xD001);  // Horizon OutOfResource
    pthread_mutex_lock(&g_klock);
    assert(!thread->started && !thread->finished && !thread->obj.signaled);
    pthread_mutex_unlock(&g_klock);

    pthread_barrier_t ready;
    assert(pthread_barrier_init(&ready, NULL, 2) == 0);
    StartAttempt attempts[2] = {{handle, 0, &ready}, {handle, 0, &ready}};
    pthread_t callers[2];
    for (int i = 0; i < 2; i++) assert(pthread_create(&callers[i], NULL, start_attempt, &attempts[i]) == 0);
    for (int i = 0; i < 2; i++) assert(pthread_join(callers[i], NULL) == 0);
    assert(pthread_barrier_destroy(&ready) == 0);
    assert((attempts[0].result == 0 && attempts[1].result == 0xFA01) ||
           (attempts[1].result == 0 && attempts[0].result == 0xFA01));
    pthread_mutex_lock(&completed_lock);
    while (!worker_completed) pthread_cond_wait(&completed_changed, &completed_lock);
    assert(worker_completed == 1);
    pthread_mutex_unlock(&completed_lock);
    cpu.x[0] = handle;
    rt_svc(&cpu, 0x09, MAIN_BASE + 0x80);
    assert(cpu.x[0] == 0xFA01);  // a completed guest thread cannot be restarted
    kernel_stop_threads(NULL);
    pthread_mutex_lock(&g_klock);
    assert(thread->started && thread->finished && thread->obj.signaled);
    pthread_mutex_unlock(&g_klock);
    kernel_reset();
}

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
    uint64_t *foreign = NULL;
    if (!strcmp(argv[2], "foreign-canonical")) {
        assert(GUEST_LO != 0x7000000000ull);
        foreign = mmap((void *)0x7000000000ull, 0x4000, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        assert(foreign == (void *)0x7000000000ull);
        *foreign = 0xcafe;
    }
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
    if (!strcmp(argv[2], "thread-resource-retry")) { thread_resource_retry(); return 0; }
    if (!strcmp(argv[2], "main-thread-resource")) {
        KThread *t = kernel_create_main_thread(MAIN_BASE + 0x80, 0x100000, 44);
        struct rlimit saved = restrict_next_host_stack();
        kernel_run_main(t, t->handle, 0, 0, 0);
        assert(setrlimit(RLIMIT_AS, &saved) == 0);
        assert(!"kernel_run_main must fail fatally when the host thread cannot start");
    }
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
        if (foreign) assert(*foreign == 0xcafe);
        // Match production teardown, then load into the same Module records.
        g_nmodules = 0;
        loader_load(argv[1]);
        kernel_init();
    }
    if (foreign) assert(munmap(foreign, 0x4000) == 0);
    return 0;
}

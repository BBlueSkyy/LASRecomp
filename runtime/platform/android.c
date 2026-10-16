// Android/Bionic host layer. Kept separate from Linux: no glibc execinfo, desktop
// launcher, or assumption that an APK's native library is its executable.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "../platform.h"
#include "android_compat.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

static void diagnostic(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
#ifdef __ANDROID__
    va_list copy;
    va_copy(copy, ap);
    __android_log_vprint(ANDROID_LOG_ERROR, "LASRecomp", format, copy);
    va_end(copy);
#endif
    vfprintf(stderr, format, ap);
    va_end(ap);
}

// ---- guest address space
// The CPU and generated code directly dereference guest addresses. An arbitrary
// host allocation cannot replace this exact window without address translation
// throughout the CPU, generated SIMD, loader, HLE and graphics runtime.
static pthread_mutex_t guest_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t guest_lo, guest_hi, guest_page;

static bool valid_window(uint64_t lo, uint64_t hi) {
    long page = sysconf(_SC_PAGESIZE);
    return page > 0 && lo != 0 && hi > lo && hi <= UINTPTR_MAX &&
           hi - lo <= SIZE_MAX && lo % (uint64_t)page == 0 && hi % (uint64_t)page == 0;
}

static bool owned_range(uint64_t addr, uint64_t size) {
    uint64_t hi = __atomic_load_n(&guest_hi, __ATOMIC_ACQUIRE);
    // Publication happens once, before guest execution. No mutex is permitted
    // here: protection also runs inside the data-watchpoint signal handler.
    return hi && size && addr >= guest_lo && addr < hi && size <= hi - addr;
}

// /proc is diagnostic only: a maps snapshot cannot make MAP_FIXED safe against
// concurrent allocations. Always let mmap itself establish exclusive ownership.
static void report_reservation_failure(uint64_t lo, uint64_t hi, int error) {
    diagnostic("Android: cannot reserve exact guest window 0x%" PRIx64 "..0x%" PRIx64 ": %s (errno %d)\n",
               lo, hi, strerror(error), error);
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        diagnostic("Android: cannot read /proc/self/maps: %s\n", strerror(errno));
        errno = error;
        return;
    }
    char line[2048];
    unsigned count = 0;
    while (fgets(line, sizeof line, maps)) {
        uint64_t start, end;
        if (sscanf(line, "%" SCNx64 "-%" SCNx64, &start, &end) != 2 || start >= hi || end <= lo) continue;
        // Keep the actual VMA (including its backing name) and the intersection.
        diagnostic("Android: overlap 0x%" PRIx64 "..0x%" PRIx64 ": %s%s",
                   start > lo ? start : lo, end < hi ? end : hi,
                   line, strchr(line, '\n') ? "" : "\n");
        ++count;
    }
    fclose(maps);
    if (!count) diagnostic("Android: no overlapping VMA in /proc/self/maps; reservation may be limited by address-space policy or resources\n");
    errno = error;
}

bool plat_reserve_guest(uint64_t lo, uint64_t hi) {
    if (!valid_window(lo, hi)) { errno = EINVAL; return false; }
    pthread_mutex_lock(&guest_lock);
    if (guest_hi) {
        bool same = guest_lo == lo && guest_hi == hi;
        pthread_mutex_unlock(&guest_lock);
        if (!same) errno = EALREADY;
        return same;
    }
    size_t size = (size_t)(hi - lo);
    void *address = (void *)(uintptr_t)lo;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
    void *p = mmap(address, size, PROT_READ | PROT_WRITE, flags | MAP_FIXED_NOREPLACE, -1, 0);
    // Old Android kernels either reject NOREPLACE or ignore it as an unknown
    // flag. A plain hint is safe even then: accept only the exact address and
    // immediately discard any alternative mapping. Never use MAP_FIXED here.
    bool retry = p == MAP_FAILED && errno == EINVAL;
    if (p != MAP_FAILED && p != address) { munmap(p, size); p = MAP_FAILED; retry = true; }
    if (retry) p = mmap(address, size, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (p != MAP_FAILED && p != address) {
        munmap(p, size);
        p = MAP_FAILED;
        errno = EADDRNOTAVAIL;
    }
    if (p == MAP_FAILED) {
        int error = errno;
        pthread_mutex_unlock(&guest_lock);
        report_reservation_failure(lo, hi, error);
        return false;
    }
    guest_lo = lo;
    guest_page = (uint64_t)sysconf(_SC_PAGESIZE);
    __atomic_store_n(&guest_hi, hi, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&guest_lock);
    return true;
}

bool plat_reset_guest(uint64_t lo, uint64_t hi) {
    pthread_mutex_lock(&guest_lock);
    if (!guest_hi || lo != guest_lo || hi != guest_hi) {
        pthread_mutex_unlock(&guest_lock);
        errno = EINVAL;
        return false;
    }
    // Keep ownership continuously; MAP_FIXED would silently replace an unrelated
    // mapping if a caller passed the wrong window. MADV_DONTNEED guarantees fresh
    // zero pages for this private anonymous mapping, without releasing its VMAs.
    bool ok = mprotect((void *)(uintptr_t)lo, hi - lo, PROT_READ | PROT_WRITE) == 0 &&
              madvise((void *)(uintptr_t)lo, hi - lo, MADV_DONTNEED) == 0;
    int error = errno;
    pthread_mutex_unlock(&guest_lock);
    if (!ok) errno = error;
    return ok;
}

void plat_commit(uint64_t addr, uint64_t size) { (void)addr; (void)size; } // anonymous pages are demand backed

int plat_written_pages(uint64_t addr, uint64_t size, uint64_t *pages, int max, uint64_t *page) {
    (void)addr; (void)size; (void)pages; (void)max; (void)page;
    return -1; // Bionic/Linux does not provide MEM_WRITE_WATCH
}

int plat_protect(uint64_t addr, uint64_t size, int prot) {
    if (prot != PLAT_PROT_NONE && prot != PLAT_PROT_READ && prot != PLAT_PROT_RW) { errno = EINVAL; return -1; }
    if (!owned_range(addr, size) || addr % guest_page || size % guest_page) {
        errno = EINVAL;
        return -1;
    }
    int p = prot == PLAT_PROT_NONE ? PROT_NONE : prot == PLAT_PROT_READ ? PROT_READ : PROT_READ | PROT_WRITE;
    return mprotect((void *)(uintptr_t)addr, size, p);
}

// ---- time and threads (Bionic's native pthread implementation)
uint64_t plat_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) abort();
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void plat_sleep_until_ns(uint64_t t) {
    struct timespec ts = {.tv_sec = (time_t)(t / 1000000000ull), .tv_nsec = (long)(t % 1000000000ull)};
    int result;
    do result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL); while (result == EINTR);
    if (result) diagnostic("Android: clock_nanosleep: %s\n", strerror(result));
}

void plat_thread_realtime(uint64_t period_ns) {
    (void)period_ns;
    struct sched_param sp = {.sched_priority = 1};
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0) return;
    // Android apps normally lack permission for FIFO or negative nice levels.
    // This is best effort; monotonic absolute sleeps still supply real timing.
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), -10);
}

void plat_init(void) { setvbuf(stderr, NULL, _IONBF, 0); }

// ---- process
bool plat_exe_path(char *buf, size_t size) {
    if (!buf || size < 2) { errno = EINVAL; return false; }
    ssize_t n = readlink("/proc/self/exe", buf, size);
    if (n < 0) return false;
    if ((size_t)n >= size) { buf[0] = 0; errno = ENAMETOOLONG; return false; }
    buf[n] = 0;
    return n > 0;
}

void plat_exec(char *const argv[]) {
    if (argv) {
        if (!argv[0]) { errno = EINVAL; return; }
        execv(argv[0], argv);
        return;
    }
    // In an APK /proc/self/exe is app_process, not LASRecomp. Re-executing it
    // cannot recreate Java/SDL lifecycle state; the runtime uses in-place reboot.
    errno = ENOTSUP;
    diagnostic("Android: process re-exec is unavailable inside an APK; use the runtime's in-place reboot\n");
}

extern char **environ;
PlatProc plat_spawn(const char *const argv[], const char *log_path, bool append_log) {
    if (!argv || !argv[0]) { errno = EINVAL; return 0; }
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    int result = posix_spawn_file_actions_init(&fa);
    if (result) { errno = result; return 0; }
    result = posix_spawnattr_init(&at);
    if (result) { posix_spawn_file_actions_destroy(&fa); errno = result; return 0; }
    result = posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, log_path ? log_path : "/dev/null",
                                             O_WRONLY | O_CREAT | (append_log ? O_APPEND : O_TRUNC), 0600);
    if (!result) result = posix_spawn_file_actions_adddup2(&fa, STDOUT_FILENO, STDERR_FILENO);
    if (!result) result = posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP);
    if (!result) result = posix_spawnattr_setpgroup(&at, 0);
    pid_t pid = 0;
    if (!result) result = posix_spawn(&pid, argv[0], &fa, &at, (char *const *)argv, environ);
    posix_spawnattr_destroy(&at);
    posix_spawn_file_actions_destroy(&fa);
    if (result) { errno = result; return 0; }
    return (PlatProc)pid;
}

int plat_proc_wait(PlatProc process, bool block) {
    if (process <= 0 || process > INT32_MAX) { errno = EINVAL; return 255; }
    int status;
    pid_t result;
    do result = waitpid((pid_t)process, &status, block ? 0 : WNOHANG); while (result < 0 && errno == EINTR);
    if (!result) return -1;
    if (result < 0) return 255;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 255;
}

void plat_proc_kill(PlatProc process) {
    if (process <= 0 || process > INT32_MAX) return;
    // Only posix_spawn-created groups belong here; never pass zero to kill().
    kill(-(pid_t)process, SIGKILL);
    while (waitpid((pid_t)process, NULL, 0) < 0 && errno == EINTR) {}
}

static int remove_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
    (void)sb; (void)flag; (void)ftw;
    return remove(path);
}
bool plat_remove_tree(const char *dir) {
    if (!dir || !*dir) { errno = EINVAL; return false; }
    return nftw(dir, remove_entry, 16, FTW_DEPTH | FTW_PHYS) == 0;
}
void plat_terminate(void) { _exit(0); }

// ---- diagnostics (NDK libunwind instead of glibc-only execinfo.h)
// liblas_platform is loaded before liblasrecomp to reserve the guest window.
// Register runtime callbacks explicitly: unresolved imports would prevent the
// Android loader from opening this library before the runtime exists.
static bool (*runtime_watch)(uint64_t, uint64_t);
static void (*runtime_report)(const char *, uint64_t, uint64_t, uint64_t);
static const char *(*runtime_symbol)(uint64_t, uint64_t *);
void plat_android_set_runtime_hooks(bool (*watch)(uint64_t, uint64_t),
                                    void (*report)(const char *, uint64_t, uint64_t, uint64_t),
                                    const char *(*symbol)(uint64_t, uint64_t *)) {
    runtime_watch = watch;
    runtime_report = report;
    runtime_symbol = symbol;
}
const char *plat_symbol(uint64_t pc, uint64_t *offset) {
    Dl_info di = {0};
    if (!dladdr((void *)(uintptr_t)pc, &di) || !di.dli_sname) return NULL;
    if (offset) *offset = pc - (uint64_t)(uintptr_t)di.dli_saddr;
    return di.dli_sname;
}

struct Trace { uintptr_t pcs[64]; size_t count; };
static _Unwind_Reason_Code unwind_frame(struct _Unwind_Context *ctx, void *arg) {
    struct Trace *trace = arg;
    uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);
    if (pc) trace->pcs[trace->count++] = pc;
    return trace->count == sizeof trace->pcs / sizeof trace->pcs[0] ? _URC_END_OF_STACK : _URC_NO_REASON;
}
void plat_print_backtrace(void) {
    struct Trace trace = {0};
    _Unwind_Backtrace(unwind_frame, &trace);
    Dl_info runtime = {0};
    if (runtime_symbol) dladdr((void *)(uintptr_t)runtime_symbol, &runtime);
    for (size_t i = 0; i < trace.count; ++i) {
        uint64_t pc = trace.pcs[i], offset = 0;
        Dl_info di = {0};
        dladdr((void *)(uintptr_t)pc, &di);
        const char *name = plat_symbol(pc, &offset);
        if (!name && runtime_symbol && di.dli_fbase && di.dli_fbase == runtime.dli_fbase)
            name = runtime_symbol(pc, &offset);
        const char *module = di.dli_fname ? strrchr(di.dli_fname, '/') : NULL;
        module = module ? module + 1 : di.dli_fname ? di.dli_fname : "?";
        if (name) diagnostic("  #%zu %016" PRIx64 " %s+%" PRIx64 " (%s+%" PRIx64 ")\n",
                             i, pc, name, offset, module, pc - (uint64_t)(uintptr_t)di.dli_fbase);
        else diagnostic("  #%zu %016" PRIx64 " %s+%" PRIx64 "\n",
                        i, pc, module, pc - (uint64_t)(uintptr_t)di.dli_fbase);
    }
}
void plat_fatal_notice(void) { diagnostic("LAS Recomp stopped because of an error. See the LASRecomp log for details.\n"); }

static _Thread_local volatile sig_atomic_t handling_fault;
static void crash_handler(int sig, siginfo_t *si, void *ctx) {
    if (handling_fault) _exit(128 + sig);
    handling_fault = 1;
    ucontext_t *uc = ctx;
    uint64_t pc = 0, lr = 0;
#if defined(__aarch64__)
    pc = uc->uc_mcontext.pc;
    lr = uc->uc_mcontext.regs[30];
#elif defined(__x86_64__)
    pc = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
#else
#error Android platform requires an ARM64 host (x86-64 is supported for host regression tests)
#endif
    uint64_t addr = (uint64_t)(uintptr_t)si->si_addr;
    if ((sig == SIGSEGV || sig == SIGBUS) && runtime_watch && runtime_watch(addr, pc)) { handling_fault = 0; return; }
    const char *what = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" :
                       sig == SIGFPE ? "SIGFPE" : "SIGABRT";
    if (runtime_report) runtime_report(what, addr, pc, lr);
    // Callback contract is noreturn; do not resume a fatal instruction if a
    // caller violates it or the fault happens before runtime registration.
    _exit(128 + sig);
}

struct AltStack { void *allocation; size_t size; };
static pthread_key_t altstack_key;
static pthread_once_t crash_once = PTHREAD_ONCE_INIT;
static int crash_init_error;
static void free_altstack(void *value) {
    struct AltStack *stack = value;
    if (stack) {
        // A pthread destructor still runs on a live thread. Disable our stack
        // before returning its storage, including for later destructors.
        stack_t current;
        if (sigaltstack(NULL, &current) != 0) return;
        if (!(current.ss_flags & SS_DISABLE) &&
            (uintptr_t)current.ss_sp >= (uintptr_t)stack->allocation &&
            (uintptr_t)current.ss_sp < (uintptr_t)stack->allocation + stack->size) {
            stack_t disabled = {.ss_flags = SS_DISABLE};
            if (sigaltstack(&disabled, NULL) != 0) return; // retaining memory is safer than a dangling signal stack
        }
        munmap(stack->allocation, stack->size);
        free(stack);
    }
}
static void install_crash_actions(void) {
    crash_init_error = pthread_key_create(&altstack_key, free_altstack);
    if (crash_init_error) return;
    struct sigaction sa = {0};
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i)
        if (sigaction(signals[i], &sa, NULL) != 0 && !crash_init_error) crash_init_error = errno;
}
void plat_crash_handler_install(void) {
    if (!runtime_report) {
        diagnostic("Android: register runtime fault hooks before installing crash handlers\n");
        return;
    }
    pthread_once(&crash_once, install_crash_actions);
    if (crash_init_error) { diagnostic("Android: installing crash handlers: %s\n", strerror(crash_init_error)); return; }
    stack_t current;
    if (sigaltstack(NULL, &current) != 0) { diagnostic("Android: querying signal stack: %s\n", strerror(errno)); return; }
    // Respect an alternate stack already supplied by Android/SDL on this thread.
    if (!(current.ss_flags & SS_DISABLE)) return;
    size_t page = (size_t)sysconf(_SC_PAGESIZE), usable = 1u << 20;
    if (!page || page > usable) { diagnostic("Android: unsupported signal-stack page size\n"); return; }
    size_t total = usable + 2 * page;
    struct AltStack *owned = malloc(sizeof *owned);
    if (!owned) { diagnostic("Android: allocating signal-stack metadata: %s\n", strerror(errno)); return; }
    void *allocation = mmap(NULL, total, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (allocation == MAP_FAILED) { free(owned); diagnostic("Android: allocating signal stack: %s\n", strerror(errno)); return; }
    stack_t stack = {.ss_sp = (char *)allocation + page, .ss_size = usable};
    if (mprotect(stack.ss_sp, usable, PROT_READ | PROT_WRITE) != 0 || sigaltstack(&stack, NULL) != 0) {
        int error = errno;
        munmap(allocation, total); free(owned);
        diagnostic("Android: enabling signal stack: %s\n", strerror(error)); return;
    }
    owned->allocation = allocation;
    owned->size = total;
    int result = pthread_setspecific(altstack_key, owned);
    if (result) {
        stack_t disabled = {.ss_flags = SS_DISABLE};
        sigaltstack(&disabled, NULL);
        free_altstack(owned);
        diagnostic("Android: retaining signal stack: %s\n", strerror(result));
    }
}

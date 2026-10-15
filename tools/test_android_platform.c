// Host regression tests for the Android platform's POSIX implementations.
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../runtime/platform.h"
#include "../runtime/platform/android_compat.h"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
static int mmap_mode, unexpected_mappings;
static void *unexpected_mapping;
void *__real_mmap(void *, size_t, int, int, int, off_t);
int __real_munmap(void *, size_t);
int __wrap_munmap(void *a, size_t n) {
    int result = __real_munmap(a, n);
    if (!result && a == unexpected_mapping) { unexpected_mapping = NULL; --unexpected_mappings; }
    return result;
}
void *__wrap_mmap(void *a, size_t n, int p, int f, int fd, off_t off) {
    if (f & MAP_FIXED_NOREPLACE) {
        if (mmap_mode == 1) { errno = EINVAL; return MAP_FAILED; }
        if (mmap_mode == 2) {
            void *result = __real_mmap(NULL, n, p, f & ~MAP_FIXED_NOREPLACE, fd, off);
            assert(result != MAP_FAILED);
            if (result == a) {
                void *other = __real_mmap(NULL, n, p, f & ~MAP_FIXED_NOREPLACE, fd, off);
                assert(other != MAP_FAILED && other != a);
                assert(__real_munmap(result, n) == 0);
                result = other;
            }
            unexpected_mapping = result;
            ++unexpected_mappings;
            return result;
        }
    }
    return __real_mmap(a, n, p, f, fd, off);
}
static size_t page;
static void *watched, *worker_altstack;
static size_t worker_altstack_size;
static volatile sig_atomic_t fault_on_altstack;
bool rt_fault_watch(uint64_t a, uint64_t pc) {
    if (!pc) _exit(92);
    stack_t current;
    if (sigaltstack(NULL, &current) != 0 || !(current.ss_flags & SS_ONSTACK)) _exit(93);
    fault_on_altstack = 1;
    return watched && a >= (uintptr_t)watched && a < (uintptr_t)watched + page &&
           plat_protect((uintptr_t)watched, page, PLAT_PROT_RW) == 0;
}
void rt_fault_report(const char *what, uint64_t a, uint64_t pc, uint64_t lr) {
    (void)what; (void)a; (void)lr;
    _exit(pc ? 91 : 92);
}
const char *rt_host_fn_name(uint64_t pc, uint64_t *offset) { (void)pc; (void)offset; return NULL; }

static void memory_tests(int mode) {
    mmap_mode = mode;
    size_t n = page * 8;
    unsigned char *a = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(a != MAP_FAILED);
    memset(a + page * 3, 0xa5, page);
    assert(munmap(a, page * 3) == 0);
    assert(munmap(a + page * 4, page * 4) == 0);
    assert(!plat_reserve_guest((uintptr_t)a, (uintptr_t)a + n));
    for (size_t i = 0; i < page; ++i) assert(a[page * 3 + i] == 0xa5);
    // Refusing a reset of an unowned range must preserve the foreign mapping too.
    assert(!plat_reset_guest((uintptr_t)a, (uintptr_t)a + n));
    assert(a[page * 3] == 0xa5);
    assert(munmap(a + page * 3, page) == 0);
    assert(plat_reserve_guest((uintptr_t)a, (uintptr_t)a + n));
    assert(unexpected_mappings == 0);
    for (size_t i = 0; i < n; ++i) assert(a[i] == 0);
    memset(a, 0x3c, n);
    assert(plat_reserve_guest((uintptr_t)a, (uintptr_t)a + n));
    assert(!plat_reserve_guest((uintptr_t)a, (uintptr_t)a + n - page));
    assert(plat_protect((uintptr_t)a, page, PLAT_PROT_READ) == 0);
    assert(plat_protect((uintptr_t)a + n, page, PLAT_PROT_RW) == -1);
    assert(!plat_reset_guest((uintptr_t)a, (uintptr_t)a + page));
    assert(plat_reset_guest((uintptr_t)a, (uintptr_t)a + n));
    for (size_t i = 0; i < n; ++i) assert(a[i] == 0);
    a[0] = 1; a[n - 1] = 2;
    // One reservation per process; each case executes in a separate child.
}
static volatile sig_atomic_t received;
static void signal_received(int sig) { (void)sig; received = 1; }
static void *interrupt_sleep(void *arg) {
    pthread_t *target = arg;
    usleep(10000);
    assert(pthread_kill(*target, SIGUSR1) == 0);
    return NULL;
}
static void timing_tests(void) {
    struct sigaction sa = {.sa_handler = signal_received};
    assert(sigaction(SIGUSR1, &sa, NULL) == 0);
    pthread_t self = pthread_self(), worker;
    assert(pthread_create(&worker, NULL, interrupt_sleep, &self) == 0);
    uint64_t deadline = plat_now_ns() + 40000000;
    plat_sleep_until_ns(deadline);
    assert(plat_now_ns() >= deadline);
    assert(pthread_join(worker, NULL) == 0);
    assert(received);
    plat_sleep_until_ns(plat_now_ns() - 1);
}
static void *fault_worker(void *arg) {
    (void)arg;
    plat_crash_handler_install();
    stack_t first, second;
    assert(sigaltstack(NULL, &first) == 0 && !(first.ss_flags & SS_DISABLE));
    worker_altstack = first.ss_sp;
    worker_altstack_size = first.ss_size;
    plat_crash_handler_install();
    assert(sigaltstack(NULL, &second) == 0 && second.ss_sp == first.ss_sp);
    *(volatile unsigned char *)watched = 7;
    return NULL;
}
static void signal_tests(void) {
    watched = mmap(NULL, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(watched != MAP_FAILED);
    assert(munmap(watched, page) == 0);
    mmap_mode = 0;
    assert(plat_reserve_guest((uintptr_t)watched, (uintptr_t)watched + page));
    assert(plat_protect((uintptr_t)watched, page, PLAT_PROT_NONE) == 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, fault_worker, NULL) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(*(unsigned char *)watched == 7 && fault_on_altstack);
    unsigned char resident;
    assert(worker_altstack_size > 0);
    assert(mincore(worker_altstack, page, &resident) == -1 && errno == ENOMEM);
    watched = NULL;
    // Fatal faults must reach the runtime's real reporting callback with a PC.
    pid_t child = fork();
    assert(child >= 0);
    if (!child) { plat_crash_handler_install(); raise(SIGILL); _exit(94); }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 91);
}
static void process_tests(void) {
    char path[4096];
    assert(plat_exe_path(path, sizeof path) && path[0] == '/');
    assert(!plat_exe_path(path, 0));
    assert(!plat_exe_path(path, 1));
    assert(!plat_exe_path(path, 4));  // never accept a truncated executable path
    const char *av[] = {"/bin/sh", "-c", "exit 17", NULL};
    PlatProc p = plat_spawn(av, NULL, false);
    assert(p && plat_proc_wait(p, true) == 17);
    const char *bad[] = {"/this/path/does/not/exist", NULL};
    assert(!plat_spawn(bad, NULL, false));
    const char *slow[] = {"/bin/sh", "-c", "sleep 10", NULL};
    p = plat_spawn(slow, NULL, false);
    assert(p && plat_proc_wait(p, false) == -1);
    plat_proc_kill(p);
    assert(plat_proc_wait(p, false) == 255);
    assert(!plat_spawn(NULL, NULL, false));
    plat_proc_kill(0);  // never kill the application's own process group
    char log[] = "/tmp/lasrecomp-platform-log-XXXXXX";
    int fd = mkstemp(log);
    assert(fd >= 0 && close(fd) == 0);
    const char *output[] = {"/bin/sh", "-c", "echo out; echo err >&2", NULL};
    p = plat_spawn(output, log, false);
    assert(p && plat_proc_wait(p, true) == 0);
    p = plat_spawn(output, log, true);
    assert(p && plat_proc_wait(p, true) == 0);
    FILE *file = fopen(log, "r");
    assert(file);
    char contents[64] = {0};
    assert(fread(contents, 1, sizeof contents - 1, file) == 16);
    assert(!strcmp(contents, "out\nerr\nout\nerr\n"));
    assert(fclose(file) == 0 && unlink(log) == 0);
}
static void remove_tree_tests(void) {
    char root[] = "/tmp/lasrecomp-platform-tree-XXXXXX";
    char target[] = "/tmp/lasrecomp-platform-target-XXXXXX";
    assert(mkdtemp(root) && mkdtemp(target));
    char link[256], sentinel[256];
    snprintf(link, sizeof link, "%s/link", root);
    snprintf(sentinel, sizeof sentinel, "%s/sentinel", target);
    FILE *file = fopen(sentinel, "w");
    assert(file && fclose(file) == 0);
    assert(symlink(target, link) == 0);
    assert(plat_remove_tree(root));
    struct stat st;
    assert(stat(root, &st) == -1 && errno == ENOENT);
    assert(stat(sentinel, &st) == 0);
    assert(plat_remove_tree(target));
}
int main(void) {
    plat_android_set_runtime_hooks(rt_fault_watch, rt_fault_report, rt_host_fn_name);
    page = (size_t)sysconf(_SC_PAGESIZE);
    assert(!plat_reserve_guest(1, 2));
    assert(!plat_reserve_guest(page, page));
    for (int mode = 0; mode < 3; ++mode) {
        pid_t child = fork();
        assert(child >= 0);
        if (!child) { memory_tests(mode); _exit(0); }
        int status;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    timing_tests();
    signal_tests();
    process_tests();
    remove_tree_tests();
    uint64_t off;
    assert(plat_symbol((uintptr_t)main, &off) && off == 0);
    plat_print_backtrace();
    puts("Android platform tests passed");
}

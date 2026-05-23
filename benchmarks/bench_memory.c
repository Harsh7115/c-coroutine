/*
 * bench_memory.c -- Stack allocation and coroutine lifecycle memory benchmarks
 *
 * Measures the cost of:
 *   1. co_create / co_destroy in bulk (stack alloc + mmap overhead)
 *   2. Minimum viable stack size vs. large stacks
 *   3. Peak RSS growth per coroutine at various stack sizes
 *
 * Build:
 *   gcc -O2 -o bench_memory bench_memory.c -L.. -lcoroutine
 * Run:
 *   ./bench_memory
 *
 * Output columns:
 *   stack_kb   n_coros   total_ms   create_ns/co   rss_kb_delta
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include "../include/coroutine.h"

/* ---- timing helpers ----------------------------------------------------- */

static long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static long rss_kb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return ru.ru_maxrss / 1024;  /* macOS reports bytes */
#else
    return ru.ru_maxrss;         /* Linux reports kilobytes */
#endif
}

/* ---- dummy coroutine that just returns ----------------------------------- */

static void noop_coro(void *arg) {
    (void)arg;
    /* intentionally empty: measures pure create/destroy cost */
}

/* ---- benchmark: bulk create then run ------------------------------------ */

static void bench_bulk_create(int stack_kb, int n) {
    size_t stack_bytes = (size_t)stack_kb * 1024;
    long rss_before = rss_kb();
    long t0 = now_ns();

    for (int i = 0; i < n; i++) {
        co_id_t id = co_create(noop_coro, NULL, stack_bytes);
        if (id < 0) {
            fprintf(stderr, "co_create failed at i=%d\n", i);
            return;
        }
    }

    long t_created = now_ns();
    co_run();
    long t_done = now_ns();
    long rss_after = rss_kb();

    double create_ms  = (double)(t_created - t0)    / 1e6;
    double total_ms   = (double)(t_done    - t0)    / 1e6;
    double ns_per_co  = (double)(t_created - t0)    / n;
    long   rss_delta  = rss_after - rss_before;

    printf("  %6d KB   %6d coros   create=%7.2f ms   run=%7.2f ms   "
           "%.0f ns/co   rss_delta=%+ld KB\n",
           stack_kb, n, create_ms, total_ms, ns_per_co, rss_delta);
}

/* ---- benchmark: sequential create-run-destroy (one at a time) ----------- */

static void bench_sequential(int stack_kb, int n) {
    size_t stack_bytes = (size_t)stack_kb * 1024;
    long t0 = now_ns();

    for (int i = 0; i < n; i++) {
        co_id_t id = co_create(noop_coro, NULL, stack_bytes);
        if (id < 0) { fprintf(stderr, "co_create failed\n"); return; }
        co_run();
    }

    long t1 = now_ns();
    double total_ms  = (double)(t1 - t0) / 1e6;
    double ns_per_co = (double)(t1 - t0) / n;

    printf("  %6d KB   %6d seq    total=%7.2f ms   %.0f ns/co\n",
           stack_kb, n, total_ms, ns_per_co);
}

/* ---- main --------------------------------------------------------------- */

int main(void) {
    printf("=== bench_memory: coroutine stack allocation cost ===\n\n");

    /* --- bulk create: vary stack size ------------------------------------ */
    printf("[bulk create + run, n=1000]\n");
    printf("  %6s     %6s         %s           %s          %s         %s\n",
           "stack", "count", "create", "total", "ns/co", "rss_delta");
    bench_bulk_create(8,    1000);
    bench_bulk_create(16,   1000);
    bench_bulk_create(32,   1000);
    bench_bulk_create(64,   1000);
    bench_bulk_create(128,  1000);
    bench_bulk_create(256,  500);
    bench_bulk_create(512,  200);
    bench_bulk_create(1024, 100);

    /* --- sequential create/run: vary count ------------------------------- */
    printf("\n[sequential create-run, stack=64 KB]\n");
    printf("  %6s     %6s         %s           %s\n",
           "stack", "count", "total", "ns/co");
    bench_sequential(64,   100);
    bench_sequential(64,   500);
    bench_sequential(64,  1000);
    bench_sequential(64,  5000);

    printf("\n=== done ===\n");
    return 0;
}

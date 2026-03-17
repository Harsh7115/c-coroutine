/*
 * bench_yield.c — micro-benchmark for co_yield round-trip latency
 *
 * Measures the average time for a single co_yield() + scheduler
 * round-trip by running N coroutines that each yield M times.
 *
 * Build (from repo root):
 *   gcc -O2 -Iinclude benchmarks/bench_yield.c -Lbuild -lcoroutine -o bench_yield
 *   # or with the ucontext backend:
 *   gcc -O2 -DUSE_UCONTEXT -Iinclude benchmarks/bench_yield.c -Lbuild -lcoroutine -o bench_yield
 *
 * Usage:
 *   ./bench_yield [num_coroutines] [yields_per_coroutine]
 *   ./bench_yield 4 1000000
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include "coroutine.h"

/* ------------------------------------------------------------------ */
/* Configuration (overridden by argv)                                  */
/* ------------------------------------------------------------------ */
static int   g_yields   = 100000;

/* ------------------------------------------------------------------ */
/* Worker coroutine                                                    */
/* ------------------------------------------------------------------ */
static void worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < g_yields; ++i)
        co_yield();
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static uint64_t ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    int num_coroutines = 4;

    if (argc > 1) num_coroutines = atoi(argv[1]);
    if (argc > 2) g_yields       = atoi(argv[2]);

    if (num_coroutines < 1 || g_yields < 1) {
        fprintf(stderr, "usage: %s [num_coroutines] [yields_per_coroutine]\n",
                argv[0]);
        return 1;
    }

    printf("bench_yield: %d coroutine(s) x %d yields\n",
           num_coroutines, g_yields);

    /* Spawn all coroutines before starting the clock so that
     * allocation cost is not measured.                        */
    for (int i = 0; i < num_coroutines; ++i) {
        Coroutine *co = co_create(worker, NULL, 0);
        if (!co) { perror("co_create"); return 1; }
    }

    uint64_t t0 = ns_now();
    co_run();
    uint64_t t1 = ns_now();

    uint64_t total_yields = (uint64_t)num_coroutines * (uint64_t)g_yields;
    uint64_t elapsed_ns   = t1 - t0;
    double   ns_per_yield = (double)elapsed_ns / (double)total_yields;
    double   mops         = (double)total_yields / ((double)elapsed_ns / 1e9) / 1e6;

    printf("  total yields : %llu\n",  (unsigned long long)total_yields);
    printf("  elapsed      : %.3f ms\n", (double)elapsed_ns / 1e6);
    printf("  ns / yield   : %.1f ns\n", ns_per_yield);
    printf("  throughput   : %.1f M yields/s\n", mops);

    return 0;
}

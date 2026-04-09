/**
 * benchmarks/bench_scheduler.c
 *
 * Scheduler throughput benchmark for c-coroutine.
 *
 * Measures how many round-robin scheduler ticks can be performed per second
 * by creating N coroutines that each yield a fixed number of times before
 * returning.  Reports:
 *   - Total elapsed wall time (ns)
 *   - Total context switches performed
 *   - Throughput (switches / second)
 *   - Amortised cost per switch (ns)
 *
 * Build:
 *   gcc -O2 -o bench_scheduler bench_scheduler.c -I../include -L.. -lcoroutine
 *
 * Usage:
 *   ./bench_scheduler [num_coroutines] [yields_per_coroutine]
 *   Defaults: 1000 coroutines, 1000 yields each  (1 million switches total)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "coroutine.h"

/* ---------- timing helpers ----------------------------------------------- */

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ---------- workload --------------------------------------------------------
 * Each coroutine receives a pointer to a shared counter and the number of
 * times it should yield before finishing.
 */

typedef struct {
    volatile uint64_t *switch_count;
    int                yields_left;
} WorkArgs;

static void worker_fn(void *arg) {
    WorkArgs *w = (WorkArgs *)arg;

    while (w->yields_left-- > 0) {
        (*w->switch_count)++;
        co_yield();
    }
    /* final switch back to scheduler counted on return */
    (*w->switch_count)++;
}

/* ---------- benchmark ------------------------------------------------------- */

static void run_benchmark(int n_coros, int yields_each) {
    volatile uint64_t switch_count = 0;

    /* Allocate argument structs */
    WorkArgs *args = calloc(n_coros, sizeof(WorkArgs));
    if (!args) { perror("calloc"); exit(EXIT_FAILURE); }

    coroutine_t **coros = calloc(n_coros, sizeof(coroutine_t *));
    if (!coros) { perror("calloc"); exit(EXIT_FAILURE); }

    for (int i = 0; i < n_coros; i++) {
        args[i].switch_count = &switch_count;
        args[i].yields_left  = yields_each;
        coros[i] = co_create(worker_fn, &args[i], 16 * 1024);
        if (!coros[i]) {
            fprintf(stderr, "co_create failed at index %d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    printf("Running: %d coroutines x %d yields = %llu expected switches\n",
           n_coros, yields_each, (unsigned long long)n_coros * yields_each);

    uint64_t t_start = now_ns();
    co_scheduler();   /* runs until all coroutines are done */
    uint64_t t_end   = now_ns();

    uint64_t elapsed = t_end - t_start;
    uint64_t total   = switch_count;
    double   tput    = (total * 1e9) / (double)elapsed;
    double   ns_each = (total > 0) ? (double)elapsed / (double)total : 0.0;

    printf("\nResults\n");
    printf("  Elapsed           : %llu ns (%.3f ms)\n",
           (unsigned long long)elapsed, elapsed / 1e6);
    printf("  Context switches  : %llu\n", (unsigned long long)total);
    printf("  Throughput        : %.2f M switches/sec\n", tput / 1e6);
    printf("  Cost per switch   : %.1f ns\n", ns_each);

    for (int i = 0; i < n_coros; i++) co_free(coros[i]);
    free(coros);
    free(args);
}

/* ---------- main ----------------------------------------------------------- */

int main(int argc, char *argv[]) {
    int n_coros      = (argc > 1) ? atoi(argv[1]) : 1000;
    int yields_each  = (argc > 2) ? atoi(argv[2]) : 1000;

    if (n_coros <= 0 || yields_each <= 0) {
        fprintf(stderr, "Usage: %s [num_coroutines] [yields_per_coroutine]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    printf("=== c-coroutine scheduler throughput benchmark ===\n\n");
    run_benchmark(n_coros, yields_each);
    printf("\nDone.\n");
    return EXIT_SUCCESS;
}

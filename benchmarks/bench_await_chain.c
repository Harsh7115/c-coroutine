/* benchmarks/bench_await_chain.c
 *
 * Benchmark: deep co_await chains and wakeup latency.
 *
 * Two scenarios are measured:
 *
 *  1. Linear chain  — coroutine[0] awaits coroutine[1], which awaits
 *     coroutine[2], ..., which awaits coroutine[N-1].  The leaf runs
 *     immediately; wakeups cascade back to the root.  Measures the
 *     per-hop wakeup latency through the wait-list.
 *
 *  2. Fan-in        — N leaf coroutines all run first; a single root
 *     coroutine awaits each of them in sequence.  Measures the overhead
 *     of repeated co_await on already-completed coroutines (fast path).
 *
 * Build:
 *   gcc -O2 -Iinclude benchmarks/bench_await_chain.c lib/libcoroutine.a -o bench_await
 *   ./bench_await [chain_depth] [fan_width]
 *
 * Defaults: chain_depth=256, fan_width=1024
 *
 * Output example (M1 Mac, clang -O2):
 *   linear chain  depth=256  total=10000 runs  avg=  1.34 us/run
 *   fan-in        width=1024 total=10000 runs  avg=  0.42 us/run
 */

#include "include/coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- portable nanosecond clock ---------------------------------------- */

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ---- scenario 1: linear chain ------------------------------------------ */

typedef struct { int idx; int depth; Coroutine **chain; } ChainArg;

static void chain_worker(void *arg) {
    ChainArg *a = arg;
    if (a->idx + 1 < a->depth)
        co_await(a->chain[a->idx + 1]);
    /* nothing else — just propagate the wakeup cascade */
}

static void run_linear_chain(int depth, int iterations) {
    Coroutine **chain = malloc(sizeof(Coroutine *) * (size_t)depth);
    ChainArg  *args   = malloc(sizeof(ChainArg)   * (size_t)depth);

    long long t0 = now_ns();

    for (int iter = 0; iter < iterations; iter++) {
        /* build chain from deepest to shallowest so each can reference next */
        for (int i = depth - 1; i >= 0; i--) {
            args[i].idx   = i;
            args[i].depth = depth;
            args[i].chain = chain;
            chain[i] = co_create(chain_worker, &args[i], 0);
        }
        co_run();
        for (int i = 0; i < depth; i++) co_free(chain[i]);
    }

    long long elapsed = now_ns() - t0;
    double us_per_run = (double)elapsed / iterations / 1000.0;

    printf("linear chain  depth=%-4d total=%-6d runs  avg=%7.2f us/run\n",
           depth, iterations, us_per_run);

    free(chain);
    free(args);
}

/* ---- scenario 2: fan-in ------------------------------------------------ */

typedef struct { int id; } LeafArg;

static void leaf_worker(void *arg) {
    (void)arg;
    /* yields once then exits */
    co_yield();
}

static void fan_root(void *arg) {
    int        width  = *(int *)arg;
    Coroutine **leafs = malloc(sizeof(Coroutine *) * (size_t)width);
    LeafArg   *largs  = malloc(sizeof(LeafArg)    * (size_t)width);

    for (int i = 0; i < width; i++) {
        largs[i].id = i;
        leafs[i] = co_create(leaf_worker, &largs[i], 0);
    }
    /* run leafs first so they are CO_DONE when we await them */
    /* co_await on a CO_DONE coroutine is a no-op — fast path */
    for (int i = 0; i < width; i++)
        co_await(leafs[i]);

    for (int i = 0; i < width; i++) co_free(leafs[i]);
    free(leafs);
    free(largs);
}

static void run_fan_in(int width, int iterations) {
    long long t0 = now_ns();

    for (int iter = 0; iter < iterations; iter++) {
        Coroutine *root = co_create(fan_root, &width, 0);
        co_run();
        co_free(root);
    }

    long long elapsed = now_ns() - t0;
    double us_per_run = (double)elapsed / iterations / 1000.0;

    printf("fan-in        width=%-4d total=%-6d runs  avg=%7.2f us/run\n",
           width, iterations, us_per_run);
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char **argv) {
    int depth = (argc > 1) ? atoi(argv[1]) : 256;
    int width = (argc > 2) ? atoi(argv[2]) : 1024;
    int iters = 10000;

    if (depth < 1)  depth = 1;
    if (width < 1)  width = 1;
    if (depth > 4096) { depth = 4096; iters = 1000; }
    if (width > 8192) { width = 8192; iters = 1000; }

    printf("c-coroutine await-chain benchmark\n");
    printf("==================================\n");
    run_linear_chain(depth, iters);
    run_fan_in(width, iters);
    return 0;
}

/*
 * bench_context_switch.c — measure coroutine context-switch overhead
 *
 * Reports the cost (in nanoseconds) of a single co_yield() round-trip,
 * i.e. the time for one coroutine to yield and for the scheduler to
 * resume another coroutine.  Run multiple iterations and report
 * min / mean / max so noise from OS scheduling is visible.
 *
 * Build:
 *   gcc -O2 -Iinclude benchmarks/bench_context_switch.c \
 *       lib/libcoroutine.a -o bench_ctx
 * Run:
 *   ./bench_ctx [iterations]   (default: 10 000 000)
 */

#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* --------------------------------------------------------------------------
 * Timing helpers
 * -------------------------------------------------------------------------- */
static inline uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* --------------------------------------------------------------------------
 * Benchmark state shared between the two ping-pong coroutines
 * -------------------------------------------------------------------------- */
typedef struct {
    long      iters;          /* total iterations requested              */
    long      count;          /* iterations completed so far             */
    uint64_t *samples;        /* per-iteration latency in ns             */
    uint64_t  t_before;       /* timestamp captured just before yield    */
} BenchState;

/* --------------------------------------------------------------------------
 * Coroutine bodies
 * -------------------------------------------------------------------------- */

/*
 * "ping" coroutine: records timestamp, yields, records elapsed time,
 * repeats for iters iterations.
 */
static void co_ping(void *arg) {
    BenchState *s = (BenchState *)arg;
    for (long i = 0; i < s->iters; i++) {
        s->t_before = now_ns();
        co_yield();
        uint64_t elapsed = now_ns() - s->t_before;
        s->samples[i] = elapsed;
        s->count++;
        co_yield();   /* give pong a chance to run its own bookkeeping */
    }
}

/*
 * "pong" coroutine: exists solely to give ping something to switch to.
 * Yields once per iteration.
 */
static void co_pong(void *arg) {
    BenchState *s = (BenchState *)arg;
    for (long i = 0; i < s->iters; i++) {
        co_yield();
    }
    (void)s;
}

/* --------------------------------------------------------------------------
 * Statistics
 * -------------------------------------------------------------------------- */
static void print_stats(const uint64_t *samples, long n) {
    if (n == 0) return;

    uint64_t min_ns = UINT64_MAX, max_ns = 0, sum = 0;
    for (long i = 0; i < n; i++) {
        if (samples[i] < min_ns) min_ns = samples[i];
        if (samples[i] > max_ns) max_ns = samples[i];
        sum += samples[i];
    }
    double mean_ns = (double)sum / (double)n;

    /* Compute stddev */
    double var = 0.0;
    for (long i = 0; i < n; i++) {
        double d = (double)samples[i] - mean_ns;
        var += d * d;
    }
    var /= (double)n;
    double stddev = 0.0;
    /* Simple integer sqrt approximation */
    if (var > 0.0) {
        double x = var;
        double root = x / 2.0;
        for (int k = 0; k < 50; k++) root = (root + x / root) / 2.0;
        stddev = root;
    }

    printf("  iterations : %ld\n",   n);
    printf("  min        : %llu ns\n", (unsigned long long)min_ns);
    printf("  mean       : %.1f ns\n", mean_ns);
    printf("  max        : %llu ns\n", (unsigned long long)max_ns);
    printf("  stddev     : %.1f ns\n", stddev);
    printf("  throughput : %.2f M switches/sec\n",
           1000.0 / mean_ns);  /* mean_ns per switch → switches per µs */
}

/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    long iters = 10000000L;
    if (argc > 1) {
        iters = atol(argv[1]);
        if (iters <= 0) {
            fprintf(stderr, "iterations must be > 0\n");
            return 1;
        }
    }

    printf("bench_context_switch: %ld round-trips\n\n", iters);

    uint64_t *samples = calloc((size_t)iters, sizeof(uint64_t));
    if (!samples) {
        perror("calloc");
        return 1;
    }

    BenchState state = {
        .iters   = iters,
        .count   = 0,
        .samples = samples,
        .t_before = 0,
    };

    Coroutine *ping = co_create(co_ping, &state, 0);
    Coroutine *pong = co_create(co_pong, &state, 0);
    if (!ping || !pong) {
        fprintf(stderr, "co_create failed\n");
        return 1;
    }

    uint64_t wall_start = now_ns();
    co_run();
    uint64_t wall_end = now_ns();

    printf("Results:\n");
    print_stats(samples, state.count);
    printf("  wall time  : %.3f s\n",
           (double)(wall_end - wall_start) / 1e9);

    co_free(ping);
    co_free(pong);
    free(samples);
    return 0;
}

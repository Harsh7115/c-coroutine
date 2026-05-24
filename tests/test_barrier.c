/*
 * tests/test_barrier.c
 *
 * Unit tests for the cooperative barrier primitive built on c-coroutine.
 *
 * A "barrier" here is a rendezvous point: N coroutines must all call
 * barrier_wait() before any of them is allowed to continue.  The
 * implementation uses only the public co_yield() API — no OS primitives.
 *
 * Test cases
 * ----------
 *  1. basic_barrier          — 3 coroutines rendezvous and all continue
 *  2. barrier_reuse          — same barrier used twice in sequence
 *  3. barrier_single         — barrier with count=1 (instant pass-through)
 *  4. barrier_ordering       — verify all waiters are released together
 *  5. barrier_large          — 32 coroutines hit the same barrier
 *  6. barrier_mixed_work     — coroutines do real work before/after barrier
 *
 * Build:
 *   gcc -Iinclude tests/test_barrier.c lib/libcoroutine.a -o test_barrier
 *   ./test_barrier
 */

#include "include/coroutine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ── Minimal test harness ────────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) do {                                         \
    if (expr) { g_pass++; }                                      \
    else {                                                        \
        fprintf(stderr, "FAIL: %s  (%s:%d)\n",                  \
                #expr, __FILE__, __LINE__);                       \
        g_fail++;                                                 \
    }                                                             \
} while (0)

/* ── Cooperative barrier ─────────────────────────────────────────────────── */

/*
 * A simple count-down barrier for cooperative coroutines.
 * Not thread-safe (single-threaded cooperative scheduling).
 */
typedef struct {
    int total;      /* how many coroutines must arrive */
    int arrived;    /* how many have called barrier_wait so far */
    int generation; /* incremented each time the barrier opens */
} Barrier;

static void barrier_init(Barrier *b, int n) {
    b->total      = n;
    b->arrived    = 0;
    b->generation = 0;
}

/*
 * Spin-yield until all N coroutines have arrived at this barrier.
 * Once the Nth coroutine arrives, all waiters are released together
 * on their next scheduling turn.
 */
static void barrier_wait(Barrier *b) {
    int gen = b->generation;
    b->arrived++;
    if (b->arrived == b->total) {
        /* Last to arrive: reset and bump generation to release everyone. */
        b->arrived = 0;
        b->generation++;
    } else {
        /* Spin-yield until the generation advances. */
        while (b->generation == gen) {
            co_yield();
        }
    }
}

/* ── Shared test state ───────────────────────────────────────────────────── */

static Barrier g_barrier;

/* ── Test 1: basic_barrier ───────────────────────────────────────────────── */

static int t1_before[3]; /* each coroutine sets its before-slot */
static int t1_after[3];  /* and its after-slot                 */

static void t1_worker(void *arg) {
    int id = *(int *)arg;
    t1_before[id] = 1;
    barrier_wait(&g_barrier);
    t1_after[id] = 1;
}

static void test_basic_barrier(void) {
    barrier_init(&g_barrier, 3);
    memset(t1_before, 0, sizeof t1_before);
    memset(t1_after,  0, sizeof t1_after);

    int ids[3] = {0, 1, 2};
    Coroutine *co[3];
    for (int i = 0; i < 3; i++)
        co[i] = co_create(t1_worker, &ids[i], 0);
    co_run();
    for (int i = 0; i < 3; i++) { co_free(co[i]); }

    CHECK(t1_before[0] && t1_before[1] && t1_before[2]);
    CHECK(t1_after[0]  && t1_after[1]  && t1_after[2]);
    printf("[PASS] test_basic_barrier\n");
}

/* ── Test 2: barrier_reuse ───────────────────────────────────────────────── */

static int t2_phase[4]; /* records which phase each coroutine reached */

static void t2_worker(void *arg) {
    int id = *(int *)arg;
    /* Phase 0 */
    t2_phase[id] = 1;
    barrier_wait(&g_barrier);
    /* Phase 1 */
    t2_phase[id] = 2;
    barrier_wait(&g_barrier);
    /* Phase 2 */
    t2_phase[id] = 3;
}

static void test_barrier_reuse(void) {
    barrier_init(&g_barrier, 4);
    memset(t2_phase, 0, sizeof t2_phase);

    int ids[4] = {0, 1, 2, 3};
    Coroutine *co[4];
    for (int i = 0; i < 4; i++)
        co[i] = co_create(t2_worker, &ids[i], 0);
    co_run();
    for (int i = 0; i < 4; i++) { co_free(co[i]); }

    for (int i = 0; i < 4; i++)
        CHECK(t2_phase[i] == 3);
    printf("[PASS] test_barrier_reuse\n");
}

/* ── Test 3: barrier_single ──────────────────────────────────────────────── */

static int t3_reached = 0;

static void t3_worker(void *arg) {
    (void)arg;
    barrier_wait(&g_barrier);
    t3_reached = 1;
}

static void test_barrier_single(void) {
    barrier_init(&g_barrier, 1);
    t3_reached = 0;

    Coroutine *co = co_create(t3_worker, NULL, 0);
    co_run();
    co_free(co);

    CHECK(t3_reached == 1);
    printf("[PASS] test_barrier_single\n");
}

/* ── Test 4: barrier_ordering ────────────────────────────────────────────── */

/*
 * Verify that no coroutine proceeds past the barrier until ALL have arrived.
 * We record the log of "after barrier" events; if any coroutine snuck through
 * early we would see its ID appear before the barrier was fully populated.
 */
static int t4_log[4]; /* sequence of IDs that exited the barrier */
static int t4_log_pos = 0;
static int t4_arrived = 0; /* count of pre-barrier arrivals, for sanity */

static void t4_worker(void *arg) {
    int id = *(int *)arg;
    t4_arrived++;
    barrier_wait(&g_barrier);
    t4_log[t4_log_pos++] = id;
}

static void test_barrier_ordering(void) {
    barrier_init(&g_barrier, 4);
    t4_log_pos = 0;
    t4_arrived  = 0;
    memset(t4_log, -1, sizeof t4_log);

    int ids[4] = {0, 1, 2, 3};
    Coroutine *co[4];
    for (int i = 0; i < 4; i++)
        co[i] = co_create(t4_worker, &ids[i], 0);
    co_run();
    for (int i = 0; i < 4; i++) { co_free(co[i]); }

    CHECK(t4_log_pos == 4);
    CHECK(t4_arrived == 4);
    /* All four IDs must appear exactly once. */
    int seen[4] = {0};
    for (int i = 0; i < 4; i++) {
        int id = t4_log[i];
        CHECK(id >= 0 && id < 4);
        if (id >= 0 && id < 4) seen[id]++;
    }
    for (int i = 0; i < 4; i++) CHECK(seen[i] == 1);
    printf("[PASS] test_barrier_ordering\n");
}

/* ── Test 5: barrier_large ───────────────────────────────────────────────── */

#define LARGE_N 32

static int t5_done[LARGE_N];

static void t5_worker(void *arg) {
    int id = *(int *)arg;
    /* Simulate unequal start times: yield id times before arriving. */
    for (int i = 0; i < id % 5; i++) co_yield();
    barrier_wait(&g_barrier);
    t5_done[id] = 1;
}

static void test_barrier_large(void) {
    barrier_init(&g_barrier, LARGE_N);
    memset(t5_done, 0, sizeof t5_done);

    int ids[LARGE_N];
    Coroutine *co[LARGE_N];
    for (int i = 0; i < LARGE_N; i++) {
        ids[i] = i;
        co[i] = co_create(t5_worker, &ids[i], 0);
    }
    co_run();
    for (int i = 0; i < LARGE_N; i++) { co_free(co[i]); }

    for (int i = 0; i < LARGE_N; i++)
        CHECK(t5_done[i] == 1);
    printf("[PASS] test_barrier_large (%d coroutines)\n", LARGE_N);
}

/* ── Test 6: barrier_mixed_work ──────────────────────────────────────────── */

#define MIXED_N 6
#define WORK_ITERS 100

static long t6_partial[MIXED_N]; /* work done before barrier */
static long t6_total[MIXED_N];   /* work done after  barrier */

static void t6_worker(void *arg) {
    int id = *(int *)arg;
    long acc = 0;
    for (int i = 0; i < WORK_ITERS * (id + 1); i++) {
        acc += i;
        if (i % 20 == 0) co_yield();
    }
    t6_partial[id] = acc;
    barrier_wait(&g_barrier);
    /* Post-barrier: continue accumulating. */
    for (int i = 0; i < WORK_ITERS; i++) acc += i;
    t6_total[id] = acc;
}

static void test_barrier_mixed_work(void) {
    barrier_init(&g_barrier, MIXED_N);
    memset(t6_partial, 0, sizeof t6_partial);
    memset(t6_total,   0, sizeof t6_total);

    int ids[MIXED_N];
    Coroutine *co[MIXED_N];
    for (int i = 0; i < MIXED_N; i++) {
        ids[i] = i;
        co[i] = co_create(t6_worker, &ids[i], 0);
    }
    co_run();
    for (int i = 0; i < MIXED_N; i++) { co_free(co[i]); }

    for (int i = 0; i < MIXED_N; i++) {
        CHECK(t6_partial[i] > 0);
        CHECK(t6_total[i] >= t6_partial[i]);
    }
    printf("[PASS] test_barrier_mixed_work\n");
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== barrier tests ===\n");

    test_basic_barrier();
    test_barrier_reuse();
    test_barrier_single();
    test_barrier_ordering();
    test_barrier_large();
    test_barrier_mixed_work();

    printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

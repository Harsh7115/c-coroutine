/*
 * test_semaphore.c — Unit tests for cooperative semaphore and mutex primitives
 *
 * Tests the semaphore and mutex wrappers built on top of c-coroutine's
 * co_yield / co_await scheduling model.  Each test spawns a small set of
 * coroutines, runs the scheduler to completion, and checks postconditions.
 *
 * Build:
 *   make tests            (builds all tests via the top-level Makefile)
 *   ./build/test_semaphore
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/coroutine.h"

/* -------------------------------------------------------------------------
 * Minimal test harness
 * ---------------------------------------------------------------------- */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_PASS()  do { tests_passed++; tests_run++; } while (0)
#define TEST_FAIL(msg) do {                                    \
    fprintf(stderr, "  FAIL [%s:%d]: %s\n",                  \
            __FILE__, __LINE__, (msg));                        \
    tests_failed++; tests_run++;                               \
} while (0)

#define CHECK(cond, msg)  do {                                 \
    if (cond) { TEST_PASS(); }                                 \
    else      { TEST_FAIL(msg); }                              \
} while (0)

/* -------------------------------------------------------------------------
 * Shared state for coroutine tests
 * ---------------------------------------------------------------------- */

/* A lightweight counting semaphore using yield-based busy-wait.
 * Real production code would use co_await on a condition; this version
 * is intentionally simple so the test logic stays readable. */
typedef struct {
    volatile int count;
} sem_t_coro;

static void sem_init_coro(sem_t_coro *s, int initial) { s->count = initial; }

static void sem_wait_coro(sem_t_coro *s) {
    while (s->count <= 0)
        co_yield();
    s->count--;
}

static void sem_post_coro(sem_t_coro *s) {
    s->count++;
}

/* -------------------------------------------------------------------------
 * Test 1: basic semaphore — producer/consumer ordering
 * ---------------------------------------------------------------------- */

static int t1_order[4];
static int t1_idx = 0;
static sem_t_coro t1_sem;

static void t1_consumer(void *arg) {
    (void)arg;
    sem_wait_coro(&t1_sem);          /* blocks until producer posts */
    t1_order[t1_idx++] = 2;          /* consumer runs second */
    co_yield();
    t1_order[t1_idx++] = 4;
}

static void t1_producer(void *arg) {
    (void)arg;
    t1_order[t1_idx++] = 1;          /* producer runs first */
    sem_post_coro(&t1_sem);
    co_yield();
    t1_order[t1_idx++] = 3;
}

static void test_basic_ordering(void) {
    printf("  test_basic_ordering ... ");
    memset(t1_order, 0, sizeof(t1_order));
    t1_idx = 0;
    sem_init_coro(&t1_sem, 0);

    co_t *sched = co_scheduler_new();
    co_spawn(sched, t1_consumer, NULL, 64 * 1024);
    co_spawn(sched, t1_producer, NULL, 64 * 1024);
    co_run(sched);
    co_scheduler_free(sched);

    /* Expected: 1 (producer), 2 (consumer unblocked), 3, 4 */
    CHECK(t1_order[0] == 1, "step 0 should be 1 (producer first)");
    CHECK(t1_order[1] == 2, "step 1 should be 2 (consumer after post)");
    printf("ok\n");
}

/* -------------------------------------------------------------------------
 * Test 2: semaphore with initial count > 1 (concurrent slots)
 * ---------------------------------------------------------------------- */

static int t2_slots_used = 0;
static int t2_max_concurrent = 0;
static sem_t_coro t2_sem;

static void t2_worker(void *arg) {
    (void)arg;
    sem_wait_coro(&t2_sem);
    t2_slots_used++;
    if (t2_slots_used > t2_max_concurrent)
        t2_max_concurrent = t2_slots_used;
    co_yield();   /* simulate work */
    t2_slots_used--;
    sem_post_coro(&t2_sem);
}

static void test_semaphore_count(void) {
    printf("  test_semaphore_count ... ");
    t2_slots_used    = 0;
    t2_max_concurrent = 0;
    sem_init_coro(&t2_sem, 2);   /* 2 concurrent slots */

    co_t *sched = co_scheduler_new();
    for (int i = 0; i < 5; i++)
        co_spawn(sched, t2_worker, NULL, 64 * 1024);
    co_run(sched);
    co_scheduler_free(sched);

    /* At most 2 workers should have been inside the critical section */
    CHECK(t2_max_concurrent <= 2,
          "max concurrent workers must not exceed semaphore count of 2");
    printf("ok\n");
}

/* -------------------------------------------------------------------------
 * Test 3: mutex — exclusive access to a shared counter
 * ---------------------------------------------------------------------- */

static int t3_counter = 0;
static sem_t_coro t3_mutex;   /* binary semaphore as mutex */

static void t3_inc(void *arg) {
    int iters = *(int *)arg;
    for (int i = 0; i < iters; i++) {
        sem_wait_coro(&t3_mutex);
        int tmp = t3_counter;
        co_yield();              /* yield while "holding" the lock */
        t3_counter = tmp + 1;
        sem_post_coro(&t3_mutex);
        co_yield();
    }
}

static void test_mutex_exclusion(void) {
    printf("  test_mutex_exclusion ... ");
    t3_counter = 0;
    sem_init_coro(&t3_mutex, 1);   /* mutex starts unlocked */

    int iters = 10;
    co_t *sched = co_scheduler_new();
    co_spawn(sched, t3_inc, &iters, 64 * 1024);
    co_spawn(sched, t3_inc, &iters, 64 * 1024);
    co_run(sched);
    co_scheduler_free(sched);

    /* With proper mutual exclusion the counter must equal 2 * iters */
    CHECK(t3_counter == 2 * iters,
          "counter should equal 2*iters with mutex protecting it");
    printf("ok\n");
}

/* -------------------------------------------------------------------------
 * Test 4: post before wait (semaphore pre-signalled)
 * ---------------------------------------------------------------------- */

static int t4_ran = 0;
static sem_t_coro t4_sem;

static void t4_waiter(void *arg) {
    (void)arg;
    sem_wait_coro(&t4_sem);   /* should not block — count already 1 */
    t4_ran = 1;
}

static void test_post_before_wait(void) {
    printf("  test_post_before_wait ... ");
    t4_ran = 0;
    sem_init_coro(&t4_sem, 1);   /* pre-signalled */

    co_t *sched = co_scheduler_new();
    co_spawn(sched, t4_waiter, NULL, 64 * 1024);
    co_run(sched);
    co_scheduler_free(sched);

    CHECK(t4_ran == 1, "waiter should have run without blocking");
    printf("ok\n");
}

/* -------------------------------------------------------------------------
 * Test 5: multiple waiters released in FIFO order
 * ---------------------------------------------------------------------- */

#define T5_N 4
static int t5_release_order[T5_N];
static int t5_rel_idx = 0;
static sem_t_coro t5_sem;

static void t5_waiter(void *arg) {
    int id = *(int *)arg;
    sem_wait_coro(&t5_sem);
    t5_release_order[t5_rel_idx++] = id;
}

static void t5_releaser(void *arg) {
    (void)arg;
    co_yield();   /* let all waiters block first */
    for (int i = 0; i < T5_N; i++) {
        sem_post_coro(&t5_sem);
        co_yield();
    }
}

static void test_fifo_release(void) {
    printf("  test_fifo_release ... ");
    t5_rel_idx = 0;
    memset(t5_release_order, -1, sizeof(t5_release_order));
    sem_init_coro(&t5_sem, 0);

    static int ids[T5_N] = {0, 1, 2, 3};
    co_t *sched = co_scheduler_new();
    for (int i = 0; i < T5_N; i++)
        co_spawn(sched, t5_waiter, &ids[i], 64 * 1024);
    co_spawn(sched, t5_releaser, NULL, 64 * 1024);
    co_run(sched);
    co_scheduler_free(sched);

    /* Coroutines are scheduled FIFO, so release order should match spawn order */
    int ordered = 1;
    for (int i = 0; i < T5_N; i++) {
        if (t5_release_order[i] != i) { ordered = 0; break; }
    }
    CHECK(ordered == 1, "waiters should be released in FIFO spawn order");
    printf("ok\n");
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void) {
    printf("=== test_semaphore ===\n");

    test_basic_ordering();
    test_semaphore_count();
    test_mutex_exclusion();
    test_post_before_wait();
    test_fifo_release();

    printf("\nResults: %d/%d passed", tests_passed, tests_run);
    if (tests_failed)
        printf(", %d FAILED", tests_failed);
    printf("\n");

    return tests_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

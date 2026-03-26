/**
 * test_scheduler.c — Unit tests for the c-coroutine FIFO scheduler
 *
 * Verifies:
 *   1. Round-robin ordering across N coroutines
 *   2. co_yield() re-enqueues and yields to the next coroutine
 *   3. Completed coroutines are removed from the run queue
 *   4. co_run() returns only when all scheduled coroutines finish
 *   5. Nested co_await() bypasses the scheduler correctly
 *
 * Build:
 *   gcc -O1 -g -o test_scheduler test_scheduler.c -I../include -L../ -lcoroutine
 * Run:
 *   ./test_scheduler
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "coroutine.h"

#define PASS(name)  printf("  PASS  %s\n", (name))
#define FAIL(name)  do { fprintf(stderr, "  FAIL  %s  (line %d)\n", (name), __LINE__); g_failures++; } while(0)
#define CHECK(expr, name) do { if (!(expr)) FAIL(name); else PASS(name); } while(0)

static int g_failures = 0;

/* Test 1: Round-robin ordering */
#define RR_N 5
static int rr_order[RR_N * 3];
static int rr_pos = 0;

static void rr_coro(void *arg)
{
    int id = *(int *)arg;
    for (int i = 0; i < 3; i++) {
        rr_order[rr_pos++] = id;
        co_yield();
    }
}

static void test_round_robin(void)
{
    printf("\n[test_round_robin]\n");
    memset(rr_order, -1, sizeof(rr_order));
    rr_pos = 0;

    int ids[RR_N];
    coroutine_t *coros[RR_N];
    for (int i = 0; i < RR_N; i++) {
        ids[i] = i;
        coros[i] = co_create(rr_coro, &ids[i], 32 * 1024);
        assert(coros[i]);
        co_schedule(coros[i]);
    }
    co_run();

    int ok = 1;
    for (int round = 0; round < 3 && ok; round++)
        for (int i = 0; i < RR_N && ok; i++)
            if (rr_order[round * RR_N + i] != i) ok = 0;
    CHECK(ok, "round-robin ordering: 0..4 repeated 3 times");
    for (int i = 0; i < RR_N; i++) co_destroy(coros[i]);
}

/* Test 2: Completion removes coro from queue */
static int finish_count = 0;

static void finishing_coro(void *arg)
{
    int steps = *(int *)arg;
    for (int i = 0; i < steps; i++) co_yield();
    finish_count++;
}

static void test_completion(void)
{
    printf("\n[test_completion]\n");
    finish_count = 0;
    int steps[] = {1, 2, 3, 0, 5};
    int n = (int)(sizeof(steps) / sizeof(steps[0]));
    coroutine_t *coros[5];
    for (int i = 0; i < n; i++) {
        coros[i] = co_create(finishing_coro, &steps[i], 32 * 1024);
        assert(coros[i]);
        co_schedule(coros[i]);
    }
    co_run();
    CHECK(finish_count == n, "all coroutines ran to completion");
    for (int i = 0; i < n; i++) co_destroy(coros[i]);
}

/* Test 3: co_run() returns when queue empties */
static int empty_queue_ok = 0;

static void single_coro(void *arg)
{
    (void)arg;
    co_yield();
    co_yield();
    empty_queue_ok = 1;
}

static void test_run_returns(void)
{
    printf("\n[test_run_returns]\n");
    empty_queue_ok = 0;
    coroutine_t *c = co_create(single_coro, NULL, 32 * 1024);
    assert(c);
    co_schedule(c);
    co_run();
    CHECK(empty_queue_ok == 1, "co_run() returns after last coroutine finishes");
    co_destroy(c);
}

/* Test 4: co_await bypasses scheduler */
static int await_seq[4];
static int await_pos = 0;

static void inner_coro(void *arg)
{
    (void)arg;
    await_seq[await_pos++] = 2;
    co_yield();
    await_seq[await_pos++] = 4;
}

static void outer_coro(void *arg)
{
    (void)arg;
    await_seq[await_pos++] = 1;
    coroutine_t *inner = co_create(inner_coro, NULL, 32 * 1024);
    assert(inner);
    co_await(inner);
    await_seq[await_pos++] = 3;
    co_destroy(inner);
}

static void test_await(void)
{
    printf("\n[test_await]\n");
    memset(await_seq, -1, sizeof(await_seq));
    await_pos = 0;
    coroutine_t *outer = co_create(outer_coro, NULL, 64 * 1024);
    assert(outer);
    co_schedule(outer);
    co_run();
    CHECK(await_seq[0] == 1, "outer runs first (step 1)");
    CHECK(await_seq[1] == 2, "inner runs via co_await (step 2)");
    CHECK(await_seq[2] == 3, "outer resumes after co_await (step 3)");
    co_destroy(outer);
}

int main(void)
{
    printf("=== c-coroutine scheduler tests ===\n");
    test_round_robin();
    test_completion();
    test_run_returns();
    test_await();
    printf("\n=== Results: %d failure(s) ===\n", g_failures);
    return g_failures ? 1 : 0;
}

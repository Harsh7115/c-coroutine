/*
 * test_edge_cases.c -- edge-case and boundary tests for c-coroutine
 *
 * Covers scenarios not exercised by the basic/stress/scheduler suites:
 *   - co_id() called from outside any coroutine (should return 0)
 *   - co_state() transitions: CO_READY -> CO_RUNNING -> CO_DONE
 *   - co_await() on a coroutine that is already CO_DONE
 *   - large argument struct passed through void* arg
 *   - a coroutine that yields 0 times (returns immediately)
 *   - multiple independent co_run() calls (scheduler re-entrancy)
 *   - many short-lived coroutines created/freed in a loop
 *
 * Build:
 *   gcc -O2 -Iinclude tests/test_edge_cases.c lib/libcoroutine.a -o test_edge
 *   ./test_edge
 *
 * Exit code: 0 on full pass, non-zero on first failure.
 */

#include "coroutine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ helpers */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr, msg) do {                                          \
    if (expr) {                                                        \
        printf("  PASS: %s\n", (msg));                                \
        g_pass++;                                                      \
    } else {                                                           \
        printf("  FAIL: %s  (line %d)\n", (msg), __LINE__);           \
        g_fail++;                                                      \
    }                                                                  \
} while (0)

/* ------------------------------------------------------------------ T1: co_id outside coroutine */

static void test_coid_outside(void)
{
    printf("\nT1: co_id() outside any coroutine\n");
    CHECK(co_id() == 0, "co_id() == 0 when called from main thread");
}

/* ------------------------------------------------------------------ T2: co_state transitions */

static Coroutine *g_probe_co = NULL;
static CoState    g_state_while_running;

static void state_probe_fn(void *arg)
{
    (void)arg;
    g_state_while_running = co_state(g_probe_co);
    co_yield();
}

static void test_co_state_transitions(void)
{
    printf("\nT2: co_state() transitions\n");

    g_probe_co = co_create(state_probe_fn, NULL, 0);

    CHECK(co_state(g_probe_co) == CO_READY,
          "newly created coroutine starts as CO_READY");

    co_run();

    CHECK(g_state_while_running == CO_RUNNING,
          "coroutine reports CO_RUNNING while executing");
    CHECK(co_state(g_probe_co) == CO_DONE,
          "finished coroutine transitions to CO_DONE");

    co_free(g_probe_co);
    g_probe_co = NULL;
}

/* ------------------------------------------------------------------ T3: co_await on already-done coroutine */

static int g_awaiter_reached_end = 0;

static void done_early_fn(void *arg) { (void)arg; /* returns immediately */ }

static void awaiter_fn(void *arg)
{
    Coroutine *target = (Coroutine *)arg;
    /* target is already CO_DONE; co_await should return without blocking */
    co_await(target);
    g_awaiter_reached_end = 1;
}

static void test_await_already_done(void)
{
    printf("\nT3: co_await() on an already-done coroutine\n");

    Coroutine *first = co_create(done_early_fn, NULL, 0);
    co_run();   /* run first to completion */
    CHECK(co_state(first) == CO_DONE, "target coroutine is CO_DONE before await");

    g_awaiter_reached_end = 0;
    Coroutine *waiter = co_create(awaiter_fn, first, 0);
    co_run();
    CHECK(g_awaiter_reached_end == 1,
          "awaiter completed without hanging on already-done target");

    co_free(first);
    co_free(waiter);
}

/* ------------------------------------------------------------------ T4: large argument struct */

#define MAGIC_VAL  0xCAFEBABEDEADBEEFULL

typedef struct {
    char     padding[256];
    uint64_t magic;
    int      idx;
} BigArg;

static uint64_t g_big_magic = 0;
static int      g_big_idx   = -1;

static void big_arg_fn(void *varg)
{
    BigArg *a = (BigArg *)varg;
    g_big_magic = a->magic;
    g_big_idx   = a->idx;
    co_yield();
}

static void test_large_arg(void)
{
    printf("\nT4: large argument struct through void*\n");

    BigArg arg;
    memset(arg.padding, 0xFF, sizeof(arg.padding));
    arg.magic = MAGIC_VAL;
    arg.idx   = 99;

    Coroutine *co = co_create(big_arg_fn, &arg, 0);
    co_run();

    CHECK(g_big_magic == MAGIC_VAL, "64-bit magic value survives void* round-trip");
    CHECK(g_big_idx   == 99,        "integer index survives void* round-trip");

    co_free(co);
}

/* ------------------------------------------------------------------ T5: coroutine that never yields */

static int g_no_yield_done = 0;

static void no_yield_fn(void *arg)
{
    (void)arg;
    g_no_yield_done = 1;
    /* returns immediately without calling co_yield() */
}

static void test_no_yield(void)
{
    printf("\nT5: coroutine that never calls co_yield()\n");

    g_no_yield_done = 0;
    Coroutine *co = co_create(no_yield_fn, NULL, 0);
    co_run();

    CHECK(g_no_yield_done == 1,         "no-yield coroutine body executed");
    CHECK(co_state(co) == CO_DONE,      "no-yield coroutine is CO_DONE after co_run");

    co_free(co);
}

/* ------------------------------------------------------------------ T6: multiple independent co_run() calls */

static int g_run_a = 0, g_run_b = 0;

static void run_a_fn(void *arg) { (void)arg; co_yield(); g_run_a = 1; }
static void run_b_fn(void *arg) { (void)arg; co_yield(); g_run_b = 1; }

static void test_multiple_runs(void)
{
    printf("\nT6: multiple independent co_run() invocations\n");

    Coroutine *a = co_create(run_a_fn, NULL, 0);
    co_run();
    CHECK(g_run_a == 1, "first co_run() batch completed");

    Coroutine *b = co_create(run_b_fn, NULL, 0);
    co_run();
    CHECK(g_run_b == 1, "second co_run() batch completed");
    CHECK(g_run_a == 1, "first batch undisturbed by second co_run()");

    co_free(a);
    co_free(b);
}

/* ------------------------------------------------------------------ T7: many short-lived coroutines */

#define BURST_COUNT 500

static int g_burst_total = 0;

static void burst_fn(void *arg)
{
    int *counter = (int *)arg;
    (*counter)++;
}

static void test_burst_create_free(void)
{
    int i;
    printf("\nT7: burst create/free of %d short-lived coroutines\n", BURST_COUNT);

    Coroutine *cos[BURST_COUNT];
    int counter = 0;

    for (i = 0; i < BURST_COUNT; i++)
        cos[i] = co_create(burst_fn, &counter, 0);

    g_burst_total = 0;
    co_run();

    for (i = 0; i < BURST_COUNT; i++) {
        CHECK(co_state(cos[i]) == CO_DONE, "burst coroutine reached CO_DONE");
        co_free(cos[i]);
        if (g_fail > 3) { /* stop spamming on repeated failure */
            printf("  (stopping burst checks early after 3 failures)\n");
            break;
        }
    }
    CHECK(counter == BURST_COUNT, "all burst coroutines incremented counter");
}

/* ------------------------------------------------------------------ main */

int main(void)
{
    printf("c-coroutine edge-case test suite\n");
    printf("==================================\n");

    test_coid_outside();
    test_co_state_transitions();
    test_await_already_done();
    test_large_arg();
    test_no_yield();
    test_multiple_runs();
    test_burst_create_free();

    printf("\n----------------------------------\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);

    return (g_fail > 0) ? 1 : 0;
}

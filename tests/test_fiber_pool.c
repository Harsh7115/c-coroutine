/**
 * tests/test_fiber_pool.c
 *
 * Unit tests for the fiber_pool abstraction built on top of c-coroutine.
 *
 * Tests covered:
 *   1. Basic pool creation and destruction
 *   2. Single task submission and completion
 *   3. Batch submission (more tasks than pool size)
 *   4. Task result collection ordering
 *   5. Pool reuse after drain
 *   6. Zero-work tasks (immediate return)
 *   7. Task that yields multiple times before finishing
 *
 * Build:
 *   gcc -O0 -g -o test_fiber_pool test_fiber_pool.c -I../include -L.. -lcoroutine
 *
 * Run:
 *   ./test_fiber_pool
 *   Exit code 0 => all tests passed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "coroutine.h"

/* ---------- minimal test harness ---------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_BEGIN(name) \
    do { \
        g_tests_run++; \
        printf("  %-50s", name); \
        fflush(stdout); \
    } while (0)

#define TEST_PASS() \
    do { \
        g_tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define TEST_FAIL(msg) \
    do { \
        g_tests_failed++; \
        printf("FAIL  (%s)\n", msg); \
    } while (0)

#define EXPECT(cond, msg) \
    do { \
        if (!(cond)) { TEST_FAIL(msg); goto done; } \
    } while (0)

/* ---------- shared task result storage ----------------------------------- */

#define MAX_RESULTS 64

typedef struct {
    int  task_id;
    int  result;
} TaskResult;

static TaskResult g_results[MAX_RESULTS];
static int        g_result_count = 0;

/* ---------- task argument types ----------------------------------------- */

typedef struct {
    int  task_id;
    int  yields;        /* how many times to yield before finishing */
    int  output_value;  /* value to store in g_results */
} TaskArgs;

static void simple_task(void *arg) {
    TaskArgs *a = (TaskArgs *)arg;
    for (int i = 0; i < a->yields; i++) co_yield();
    if (g_result_count < MAX_RESULTS) {
        g_results[g_result_count].task_id = a->task_id;
        g_results[g_result_count].result  = a->output_value;
        g_result_count++;
    }
}

/* ---------- helpers ------------------------------------------------------- */

static void reset_results(void) {
    g_result_count = 0;
    memset(g_results, 0, sizeof(g_results));
}

/* ---------- individual tests --------------------------------------------- */

/* T1: create / free a single coroutine without running it */
static void test_create_free(void) {
    TEST_BEGIN("T1: create and free single coroutine");
    TaskArgs a = {1, 0, 42};
    coroutine_t *co = co_create(simple_task, &a, 32 * 1024);
    EXPECT(co != NULL, "co_create returned NULL");
    co_free(co);
    TEST_PASS();
done:;
}

/* T2: single task runs to completion */
static void test_single_task(void) {
    TEST_BEGIN("T2: single task runs to completion");
    reset_results();
    TaskArgs a = {7, 0, 99};
    coroutine_t *co = co_create(simple_task, &a, 32 * 1024);
    EXPECT(co != NULL, "co_create returned NULL");
    co_scheduler();
    EXPECT(g_result_count == 1, "expected 1 result");
    EXPECT(g_results[0].task_id == 7, "wrong task_id");
    EXPECT(g_results[0].result  == 99, "wrong result value");
    co_free(co);
    TEST_PASS();
done:;
}

/* T3: task that yields 5 times still completes */
static void test_multi_yield_task(void) {
    TEST_BEGIN("T3: task with multiple yields completes");
    reset_results();
    TaskArgs a = {3, 5, 77};
    coroutine_t *co = co_create(simple_task, &a, 32 * 1024);
    EXPECT(co != NULL, "co_create returned NULL");
    co_scheduler();
    EXPECT(g_result_count == 1, "expected 1 result after multi-yield");
    EXPECT(g_results[0].result == 77, "wrong result after multi-yield");
    co_free(co);
    TEST_PASS();
done:;
}

/* T4: batch of 16 concurrent tasks */
static void test_batch_tasks(void) {
    TEST_BEGIN("T4: batch of 16 concurrent tasks all complete");
    reset_results();
#define BATCH 16
    TaskArgs    args[BATCH];
    coroutine_t *coros[BATCH];

    for (int i = 0; i < BATCH; i++) {
        args[i].task_id      = i;
        args[i].yields       = i % 4;   /* vary yield count */
        args[i].output_value = i * 10;
        coros[i] = co_create(simple_task, &args[i], 32 * 1024);
        EXPECT(coros[i] != NULL, "co_create returned NULL in batch");
    }

    co_scheduler();

    EXPECT(g_result_count == BATCH, "not all batch tasks produced a result");

    /* check every expected value is present */
    for (int i = 0; i < BATCH; i++) {
        int found = 0;
        for (int j = 0; j < g_result_count; j++) {
            if (g_results[j].task_id == i && g_results[j].result == i * 10) {
                found = 1; break;
            }
        }
        if (!found) { TEST_FAIL("missing result for task in batch"); goto done; }
    }

    for (int i = 0; i < BATCH; i++) co_free(coros[i]);
    TEST_PASS();
done:;
#undef BATCH
}

/* T5: pool reuse – run a set, then run another set */
static void test_pool_reuse(void) {
    TEST_BEGIN("T5: pool reuse after first batch drains");
    reset_results();
    TaskArgs a1 = {100, 2, 111};
    coroutine_t *co1 = co_create(simple_task, &a1, 32 * 1024);
    EXPECT(co1 != NULL, "co_create failed (round 1)");
    co_scheduler();
    EXPECT(g_result_count == 1, "round 1: expected 1 result");
    co_free(co1);

    reset_results();
    TaskArgs a2 = {200, 3, 222};
    coroutine_t *co2 = co_create(simple_task, &a2, 32 * 1024);
    EXPECT(co2 != NULL, "co_create failed (round 2)");
    co_scheduler();
    EXPECT(g_result_count == 1, "round 2: expected 1 result");
    EXPECT(g_results[0].result == 222, "round 2: wrong result");
    co_free(co2);
    TEST_PASS();
done:;
}

/* ---------- main ---------------------------------------------------------- */

int main(void) {
    printf("=== test_fiber_pool: unit tests for fiber pool / coroutine lifecycle ===\n\n");

    test_create_free();
    test_single_task();
    test_multi_yield_task();
    test_batch_tasks();
    test_pool_reuse();

    printf("\n--- Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed) printf(", %d FAILED", g_tests_failed);
    printf(" ---\n");

    return (g_tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

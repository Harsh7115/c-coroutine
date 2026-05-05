/*
 * test_timer.c — Unit tests for timer-coroutine behaviour in c-coroutine
 *
 * Tests covered
 * ─────────────
 *  1. Single timer fires after the expected number of scheduler ticks.
 *  2. Multiple independent timers fire in deadline order.
 *  3. A cancelled timer does NOT fire after cancellation.
 *  4. A timer with a zero deadline fires on the very next tick.
 *  5. Repeated (periodic) timer fires every N ticks for M periods.
 *  6. Timer accuracy under load: N concurrent coroutines each with their
 *     own deadline; verify all fire within +/-1 tick of target.
 *
 * Build:
 *   gcc -O2 -Wall -Wextra -I../include test_timer.c ../src/coroutine.c -o test_timer
 *
 * Run:
 *   ./test_timer
 *
 * Expected output:
 *   All N tests PASS (or a FAIL line with details).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coroutine.h"

/* ── minimal test framework ────────────────────────────────────────────── */

static int g_total  = 0;
static int g_passed = 0;

#define ASSERT(cond, msg)                                          \
    do {                                                           \
        g_total++;                                                 \
        if (cond) {                                                \
            g_passed++;                                            \
            printf("  PASS  %s\n", msg);                          \
        } else {                                                   \
            printf("  FAIL  %s  (line %d)\n", msg, __LINE__);     \
        }                                                          \
    } while (0)

/* ── shared test state ─────────────────────────────────────────────────── */

typedef struct {
    int  deadline_ticks;   /* ticks to wait before "firing" */
    int  fired_at_tick;    /* set by the coroutine when it fires */
    int  cancelled;        /* set externally before the coroutine starts */
    int  periodic_period;  /* if > 0, fire every N ticks for periodic test */
    int  periodic_count;   /* number of times the periodic timer fired */
    int  periodic_limit;   /* stop after this many fires */
} TimerState;

static int g_tick = 0;   /* global scheduler tick counter */

/* ── coroutine bodies ──────────────────────────────────────────────────── */

/* Simulates a one-shot timer: yields deadline_ticks times then marks fired. */
static void one_shot_timer(void *arg)
{
    TimerState *ts = (TimerState *)arg;

    if (ts->cancelled) {
        /* Immediately exit without firing. */
        return;
    }

    int start = g_tick;
    while ((g_tick - start) < ts->deadline_ticks) {
        co_yield();
    }

    if (!ts->cancelled) {
        ts->fired_at_tick = g_tick;
    }
}

/* Simulates a periodic timer: fires every period_ticks, up to periodic_limit. */
static void periodic_timer(void *arg)
{
    TimerState *ts   = (TimerState *)arg;
    int         last = g_tick;

    while (ts->periodic_count < ts->periodic_limit) {
        while ((g_tick - last) < ts->periodic_period) {
            co_yield();
        }
        last = g_tick;
        ts->periodic_count++;
        ts->fired_at_tick = g_tick;
        co_yield();
    }
}

/* Ticker coroutine: increments g_tick on every scheduler iteration. */
static void ticker(void *arg)
{
    int *max_ticks = (int *)arg;
    while (g_tick < *max_ticks) {
        g_tick++;
        co_yield();
    }
}

/* ── test helpers ──────────────────────────────────────────────────────── */

static void reset_tick(void) { g_tick = 0; }

/* ── individual tests ──────────────────────────────────────────────────── */

/*
 * Test 1: single timer fires after exactly deadline ticks.
 */
static void test_single_timer(void)
{
    printf("\nTest 1: single one-shot timer\n");
    reset_tick();

    TimerState ts = {0};
    ts.deadline_ticks = 5;

    int max_ticks = 20;
    co_create(ticker,        &max_ticks, 32768);
    co_create(one_shot_timer, &ts,       32768);
    co_run();

    ASSERT(ts.fired_at_tick >= 5,  "timer fires at or after deadline");
    ASSERT(ts.fired_at_tick <= 6,  "timer fires within +1 tick of deadline");
}

/*
 * Test 2: two timers fire in deadline order.
 */
static void test_two_timers_ordered(void)
{
    printf("\nTest 2: two timers fire in deadline order\n");
    reset_tick();

    TimerState ts1 = {0}, ts2 = {0};
    ts1.deadline_ticks = 3;
    ts2.deadline_ticks = 7;

    int max_ticks = 20;
    co_create(ticker,        &max_ticks, 32768);
    co_create(one_shot_timer, &ts1,      32768);
    co_create(one_shot_timer, &ts2,      32768);
    co_run();

    ASSERT(ts1.fired_at_tick > 0,                "first timer fired");
    ASSERT(ts2.fired_at_tick > 0,                "second timer fired");
    ASSERT(ts1.fired_at_tick < ts2.fired_at_tick, "first fires before second");
}

/*
 * Test 3: cancelled timer does not fire.
 */
static void test_cancelled_timer(void)
{
    printf("\nTest 3: cancelled timer does not fire\n");
    reset_tick();

    TimerState ts = {0};
    ts.deadline_ticks = 5;
    ts.cancelled      = 1;   /* cancel before it starts */

    int max_ticks = 20;
    co_create(ticker,        &max_ticks, 32768);
    co_create(one_shot_timer, &ts,       32768);
    co_run();

    ASSERT(ts.fired_at_tick == 0, "cancelled timer never fires");
}

/*
 * Test 4: zero-deadline timer fires on the very next tick.
 */
static void test_zero_deadline(void)
{
    printf("\nTest 4: zero-deadline timer fires immediately\n");
    reset_tick();

    TimerState ts = {0};
    ts.deadline_ticks = 0;

    int max_ticks = 10;
    co_create(ticker,        &max_ticks, 32768);
    co_create(one_shot_timer, &ts,       32768);
    co_run();

    ASSERT(ts.fired_at_tick <= 1, "zero-deadline fires on tick 0 or 1");
}

/*
 * Test 5: periodic timer fires every N ticks for M periods.
 */
static void test_periodic_timer(void)
{
    printf("\nTest 5: periodic timer\n");
    reset_tick();

    TimerState ts = {0};
    ts.periodic_period = 4;
    ts.periodic_limit  = 3;

    int max_ticks = 50;
    co_create(ticker,        &max_ticks, 32768);
    co_create(periodic_timer, &ts,       32768);
    co_run();

    ASSERT(ts.periodic_count == 3, "periodic timer fired exactly 3 times");
    /* Last fire should be near tick 4*3 = 12 */
    ASSERT(ts.fired_at_tick >= 11 && ts.fired_at_tick <= 14,
           "last periodic fire within expected window");
}

/*
 * Test 6: concurrent timers all fire within tolerance.
 */
#define N_CONCURRENT 8
static void test_concurrent_timers(void)
{
    printf("\nTest 6: %d concurrent timers accuracy\n", N_CONCURRENT);
    reset_tick();

    TimerState ts[N_CONCURRENT];
    memset(ts, 0, sizeof ts);

    int max_ticks = 100;
    co_create(ticker, &max_ticks, 32768);

    for (int i = 0; i < N_CONCURRENT; i++) {
        ts[i].deadline_ticks = (i + 1) * 5;   /* 5, 10, 15, ... 40 */
        co_create(one_shot_timer, &ts[i], 32768);
    }
    co_run();

    int all_ok = 1;
    for (int i = 0; i < N_CONCURRENT; i++) {
        int target = (i + 1) * 5;
        if (ts[i].fired_at_tick < target || ts[i].fired_at_tick > target + 2) {
            printf("    timer %d: expected ~%d, got %d\n",
                   i, target, ts[i].fired_at_tick);
            all_ok = 0;
        }
    }
    ASSERT(all_ok, "all concurrent timers fire within +2 ticks of deadline");
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== c-coroutine timer unit tests ===\n");

    test_single_timer();
    test_two_timers_ordered();
    test_cancelled_timer();
    test_zero_deadline();
    test_periodic_timer();
    test_concurrent_timers();

    printf("\n=== Results: %d / %d passed ===\n", g_passed, g_total);
    return (g_passed == g_total) ? 0 : 1;
}

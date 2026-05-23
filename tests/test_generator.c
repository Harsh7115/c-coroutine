/*
 * test_generator.c -- Unit tests for the coroutine-based generator pattern
 *
 * Tests the generator idiom: a coroutine that yields a sequence of values
 * to its caller via co_yield, acting like a Python generator.
 *
 * Build:
 *   gcc -O2 -o test_generator test_generator.c -L.. -lcoroutine
 * Run:
 *   ./test_generator
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../include/coroutine.h"

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT_EQ(a, b, msg) do { \
    if ((a) == (b)) { g_pass++; printf("  PASS  %s\n", msg); } \
    else { g_fail++; printf("  FAIL  %s (got %d want %d)\n", msg, (int)(a), (int)(b)); } \
} while (0)

/* --- generator: integers 0..n-1 ---------------------------------------- */

typedef struct { int n; int *out; int idx; } RangeCtx;

static void gen_range(void *arg) {
    RangeCtx *c = (RangeCtx *)arg;
    for (int i = 0; i < c->n; i++) {
        c->out[c->idx++] = i;
        co_yield();
    }
}

static void test_range_generator(void) {
    printf("\n[test_range_generator]\n");
    int out[8] = {0};
    RangeCtx ctx = { .n = 8, .out = out, .idx = 0 };
    assert(co_create(gen_range, &ctx, 32 * 1024) >= 0);
    co_run();
    EXPECT_EQ(ctx.idx, 8, "produced 8 values");
    for (int i = 0; i < 8; i++) {
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "out[%d]==%d", i, i);
        EXPECT_EQ(out[i], i, lbl);
    }
}

/* --- generator: Fibonacci ------------------------------------------------ */

typedef struct { int *out; int cap; int idx; } FibCtx;

static void gen_fib(void *arg) {
    FibCtx *c = (FibCtx *)arg;
    long a = 0, b = 1;
    while (c->idx < c->cap) {
        c->out[c->idx++] = (int)a;
        co_yield();
        long t = a + b; a = b; b = t;
    }
}

static void test_fibonacci_generator(void) {
    printf("\n[test_fibonacci_generator]\n");
    static const int want[] = {0,1,1,2,3,5,8,13,21,34};
    int out[10] = {0};
    FibCtx ctx = { .out = out, .cap = 10, .idx = 0 };
    assert(co_create(gen_fib, &ctx, 32 * 1024) >= 0);
    co_run();
    EXPECT_EQ(ctx.idx, 10, "produced 10 Fibonacci values");
    for (int i = 0; i < 10; i++) {
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "fib[%d]==%d", i, want[i]);
        EXPECT_EQ(out[i], want[i], lbl);
    }
}

/* --- generator: multiple independent generators -------------------------- */

typedef struct { int start; int step; int n; int *out; int idx; } StepCtx;

static void gen_step(void *arg) {
    StepCtx *c = (StepCtx *)arg;
    int v = c->start;
    for (int i = 0; i < c->n; i++) {
        c->out[c->idx++] = v;
        v += c->step;
        co_yield();
    }
}

static void test_multiple_generators(void) {
    printf("\n[test_multiple_generators]\n");
    int oa[5] = {0}, ob[5] = {0};
    StepCtx a = { .start = 0, .step = 2, .n = 5, .out = oa, .idx = 0 };
    StepCtx b = { .start = 1, .step = 3, .n = 5, .out = ob, .idx = 0 };
    assert(co_create(gen_step, &a, 32 * 1024) >= 0);
    assert(co_create(gen_step, &b, 32 * 1024) >= 0);
    co_run();
    static const int ea[] = {0,2,4,6,8};
    static const int eb[] = {1,4,7,10,13};
    EXPECT_EQ(a.idx, 5, "gen A: 5 values");
    EXPECT_EQ(b.idx, 5, "gen B: 5 values");
    for (int i = 0; i < 5; i++) {
        char la[64], lb[64];
        snprintf(la, sizeof(la), "A[%d]==%d", i, ea[i]);
        snprintf(lb, sizeof(lb), "B[%d]==%d", i, eb[i]);
        EXPECT_EQ(oa[i], ea[i], la);
        EXPECT_EQ(ob[i], eb[i], lb);
    }
}

/* --- generator: early return --------------------------------------------- */

typedef struct { int limit; int count; } EarlyCtx;

static void gen_early(void *arg) {
    EarlyCtx *c = (EarlyCtx *)arg;
    for (int i = 0; i < 100; i++) {
        c->count++;
        if (i >= c->limit - 1) return;
        co_yield();
    }
}

static void test_early_termination(void) {
    printf("\n[test_early_termination]\n");
    EarlyCtx ctx = { .limit = 5, .count = 0 };
    assert(co_create(gen_early, &ctx, 32 * 1024) >= 0);
    co_run();
    EXPECT_EQ(ctx.count, 5, "generator ran exactly limit iterations");
}

/* --- main ---------------------------------------------------------------- */

int main(void) {
    printf("=== test_generator ===\n");
    test_range_generator();
    test_fibonacci_generator();
    test_multiple_generators();
    test_early_termination();
    printf("\n--- %d passed, %d failed ---\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

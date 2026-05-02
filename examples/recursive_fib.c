/*
 * recursive_fib.c -- coroutine-based Fibonacci sequence generator
 *
 * Demonstrates two complementary patterns:
 *
 *   1. GENERATOR pattern
 *      A single "fib_gen" coroutine yields successive Fibonacci numbers
 *      to its caller via co_yield.  The caller drives the generator in a
 *      loop without any shared global state.
 *
 *   2. RECURSIVE DECOMPOSITION pattern
 *      fib(n) is split into two child coroutines, fib(n-1) and fib(n-2),
 *      each of which spawns its own children.  The scheduler runs all of
 *      them cooperatively.  Results are communicated through a tiny
 *      future-like struct that a child fills before exiting.
 *
 * Build (from repo root):
 *   make
 *   gcc -o fib_demo examples/recursive_fib.c -I include -L . -lcoroutine
 *
 * Expected output (first 16 Fibonacci numbers, two different methods):
 *   [generator]  F(0)=0  F(1)=1  F(2)=1  ...  F(15)=610
 *   [recursive]  fib(10) = 55
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coroutine.h"

/* -----------------------------------------------------------------------
 * Part 1 -- generator
 * ---------------------------------------------------------------------- */

typedef struct {
    int     limit;   /* how many numbers to yield           */
    long    out;     /* current value (written by generator)*/
    int     done;    /* set to 1 when finished              */
} FibGenState;

/* Generator coroutine: yields limit Fibonacci numbers one at a time. */
static void fib_gen(void *arg)
{
    FibGenState *s = (FibGenState *)arg;
    long a = 0, b = 1, tmp;
    int  i;

    for (i = 0; i < s->limit; i++) {
        s->out = a;
        co_yield();   /* hand control back to caller */
        tmp = a + b;
        a   = b;
        b   = tmp;
    }
    s->done = 1;
    co_yield();       /* final yield so caller can see done flag */
}

static void run_generator(int limit)
{
    FibGenState state;
    memset(&state, 0, sizeof(state));
    state.limit = limit;

    coroutine_t *gen = co_create(fib_gen, &state, 0);
    if (!gen) {
        fprintf(stderr, "co_create failed\n");
        return;
    }

    printf("[generator] first %d Fibonacci numbers:\n  ", limit);
    int idx = 0;
    while (!state.done) {
        co_resume(gen);
        if (!state.done) {
            printf("F(%d)=%ld  ", idx++, state.out);
        }
    }
    printf("\n");
    co_destroy(gen);
}

/* -----------------------------------------------------------------------
 * Part 2 -- recursive decomposition via coroutines
 *
 * Each coroutine computes fib(n) by spawning two children for n > 1.
 * A "Future" struct lets a child post its result for the parent to read.
 * ---------------------------------------------------------------------- */

typedef struct Future {
    long value;
    int  ready;
} Future;

typedef struct FibArgs {
    int     n;
    Future *result; /* where to write the answer */
} FibArgs;

static void fib_coro(void *arg)
{
    FibArgs *a = (FibArgs *)arg;
    int n = a->n;

    if (n <= 1) {
        a->result->value = n;
        a->result->ready = 1;
        return;
    }

    /* Allocate futures and argument blocks on the heap so they outlive
     * this stack frame while children are running. */
    Future   f1 = {0, 0}, f2 = {0, 0};
    FibArgs  a1 = { n - 1, &f1 };
    FibArgs  a2 = { n - 2, &f2 };

    coroutine_t *c1 = co_create(fib_coro, &a1, 0);
    coroutine_t *c2 = co_create(fib_coro, &a2, 0);

    /* Run both children to completion interleaved with co_yield so the
     * scheduler stays fair and the call stack stays shallow. */
    while (!f1.ready || !f2.ready) {
        if (!f1.ready) co_resume(c1);
        if (!f2.ready) co_resume(c2);
        if (!f1.ready || !f2.ready) co_yield();
    }

    co_destroy(c1);
    co_destroy(c2);

    a->result->value = f1.value + f2.value;
    a->result->ready = 1;
}

static long run_recursive(int n)
{
    Future  result = {0, 0};
    FibArgs args   = { n, &result };

    coroutine_t *root = co_create(fib_coro, &args, 0);
    if (!root) {
        fprintf(stderr, "co_create failed\n");
        return -1;
    }

    /* Drive the root coroutine until the answer is ready. */
    while (!result.ready) {
        co_resume(root);
    }
    co_destroy(root);
    return result.value;
}

/* -----------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
    co_init();  /* initialise the coroutine scheduler */

    /* --- Part 1: generator --- */
    run_generator(16);

    /* --- Part 2: recursive decomposition --- */
    int n = 10;
    long answer = run_recursive(n);
    printf("[recursive]  fib(%d) = %ld\n", n, answer);

    /* Spot-check a few more values */
    int checks[] = { 0, 1, 5, 15, 20 };
    long expected[] = { 0, 1, 5, 610, 6765 };
    int ok = 1;
    for (int i = 0; i < 5; i++) {
        long got = run_recursive(checks[i]);
        int pass = (got == expected[i]);
        printf("  fib(%2d) = %5ld  %s\n",
               checks[i], got, pass ? "OK" : "FAIL");
        if (!pass) ok = 0;
    }

    co_shutdown();
    return ok ? 0 : 1;
}

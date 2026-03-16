/*
 * test_stress.c — high-volume stress test
 *
 * Launches a large number of coroutines, each performing a random number of
 * yields, and verifies that every coroutine ran to completion.
 * Also exercises co_await chains.
 */

#include "../include/coroutine.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t prng_state = 0xdeadbeef;

static uint32_t prng_next(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

#define T1_N       200
#define T1_MAX_K   10

static int t1_counter;

typedef struct { int yields; } T1Args;

static void t1_body(void *arg) {
    int k = ((T1Args *)arg)->yields;
    for (int i = 0; i < k; i++) co_yield();
    t1_counter++;
}

static void test_many_yields(void) {
    fprintf(stderr, "  [stress] %d coroutines, up to %d yields each\n",
            T1_N, T1_MAX_K);

    t1_counter = 0;

    static T1Args args[T1_N];
    static Coroutine *cos[T1_N];

    for (int i = 0; i < T1_N; i++) {
        args[i].yields = (int)(prng_next() % (T1_MAX_K + 1));
        cos[i] = co_create(t1_body, &args[i], 0);
    }

    co_run();

    int ok = (t1_counter == T1_N);
    fprintf(stderr, "  counter=%d expected=%d — %s\n",
            t1_counter, T1_N, ok ? "PASS" : "FAIL");

    for (int i = 0; i < T1_N; i++) co_free(cos[i]);

    assert(ok && "stress test 1 failed");
}

#define T2_CHAIN 32

static int t2_done[T2_CHAIN];

typedef struct { int idx; Coroutine **chain; int len; } T2Args;

static void t2_body(void *arg) {
    T2Args *a = (T2Args *)arg;
    if (a->idx + 1 < a->len) {
        co_await(a->chain[a->idx + 1]);
        for (int j = a->idx + 1; j < a->len; j++)
            assert(co_state(a->chain[j]) == CO_DONE);
    }
    t2_done[a->idx] = 1;
}

static void test_await_chain(void) {
    fprintf(stderr, "  [stress] await chain of %d coroutines\n", T2_CHAIN);

    static T2Args    args[T2_CHAIN];
    static Coroutine *chain[T2_CHAIN];

    for (int i = T2_CHAIN - 1; i >= 0; i--) {
        args[i].idx   = i;
        args[i].chain = chain;
        args[i].len   = T2_CHAIN;
        chain[i] = co_create(t2_body, &args[i], 0);
    }

    co_run();

    int ok = 1;
    for (int i = 0; i < T2_CHAIN; i++) ok &= t2_done[i];
    fprintf(stderr, "  all %d done — %s\n", T2_CHAIN, ok ? "PASS" : "FAIL");
    assert(ok && "stress test 2 (await chain) failed");

    for (int i = 0; i < T2_CHAIN; i++) co_free(chain[i]);
}

int main(void) {
    fprintf(stderr, "=== test_stress ===\n");
    test_many_yields();
    test_await_chain();
    fprintf(stderr, "All stress tests passed.\n");
    return 0;
}

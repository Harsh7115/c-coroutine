/*
 * examples/generator.c — Fibonacci generator / consumer pipeline
 *
 * Demonstrates the generator pattern: one coroutine produces values lazily
 * (yielding between each), another consumes them.
 *
 * Expected output:
 *   Fibonacci (first 12): 0 1 1 2 3 5 8 13 21 34 55 89
 *   Sum: 286
 */

#include "../include/coroutine.h"
#include <stdio.h>

#define FIB_COUNT 12

typedef struct {
    long  value;
    int   ready;
} Chan;

static void fib_gen(void *arg) {
    Chan *ch = (Chan *)arg;
    long a = 0, b = 1;

    for (int i = 0; i < FIB_COUNT; i++) {
        while (ch->ready) co_yield();

        ch->value = a;
        ch->ready = 1;

        long tmp = a + b;
        a = b;
        b = tmp;

        co_yield();
    }
}

typedef struct { Chan *ch; Coroutine *gen; } ConsArgs;

static void fib_consumer(void *arg) {
    ConsArgs *a   = (ConsArgs *)arg;
    Chan     *ch  = a->ch;
    long      sum = 0;

    printf("Fibonacci (first %d):", FIB_COUNT);

    for (;;) {
        while (!ch->ready) {
            if (co_state(a->gen) == CO_DONE) goto done;
            co_yield();
        }
        printf(" %ld", ch->value);
        sum    += ch->value;
        ch->ready = 0;
    }

done:
    printf("\nSum: %ld\n", sum);
}

int main(void) {
    Chan     ch  = { .value = 0, .ready = 0 };
    ConsArgs ca  = { .ch = NULL, .gen = NULL };

    Coroutine *gen  = co_create(fib_gen,      &ch, 0);
    ca.ch  = &ch;
    ca.gen = gen;
    Coroutine *cons = co_create(fib_consumer, &ca, 0);

    co_run();

    co_free(gen);
    co_free(cons);
    return 0;
}

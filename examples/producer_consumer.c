/*
 * producer_consumer.c -- classic bounded-buffer producer/consumer with coroutines
 *
 * Two producer coroutines fill a ring buffer; two consumer coroutines drain it.
 * All coroutines cooperatively yield so no OS synchronisation primitives are needed.
 *
 * Build:
 *   make examples
 * Run:
 *   ./examples/producer_consumer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/coroutine.h"

#define BUF_SIZE  8
#define N_ITEMS   16   /* items each producer will generate */

/* ---------- ring buffer ---------- */

static int  buf[BUF_SIZE];
static int  head = 0, tail = 0, count = 0;

static int buf_full(void)  { return count == BUF_SIZE; }
static int buf_empty(void) { return count == 0; }

static void buf_push(int val) {
    buf[tail] = val;
    tail  = (tail + 1) % BUF_SIZE;
    count++;
}

static int buf_pop(void) {
    int val = buf[head];
    head  = (head + 1) % BUF_SIZE;
    count--;
    return val;
}

/* ---------- coroutine bodies ---------- */

typedef struct { int id; int start; } ProdArgs;
typedef struct { int id; int total;  } ConArgs;

static void producer(void *arg) {
    ProdArgs *a = (ProdArgs *)arg;
    for (int i = 0; i < N_ITEMS; i++) {
        /* spin-yield until there is space */
        while (buf_full()) {
            co_yield();
        }
        int val = a->start + i;
        buf_push(val);
        printf("  producer %d  PUSH %d  (buf=%d)\n", a->id, val, count);
        co_yield();
    }
    printf("producer %d done\n", a->id);
}

static void consumer(void *arg) {
    ConArgs *a = (ConArgs *)arg;
    int received = 0;
    while (received < a->total) {
        while (buf_empty()) {
            co_yield();
        }
        int val = buf_pop();
        printf("  consumer %d  POP  %d  (buf=%d)\n", a->id, val, count);
        received++;
        co_yield();
    }
    printf("consumer %d done (received %d items)\n", a->id, received);
}

/* ---------- main ---------- */

int main(void) {
    puts("=== producer/consumer demo ===");

    /* Each consumer takes half the total items */
    ProdArgs pa[2] = { {0, 100}, {1, 200} };
    ConArgs  ca[2] = { {0, N_ITEMS}, {1, N_ITEMS} };

    Coroutine *prod[2], *cons[2];
    for (int i = 0; i < 2; i++) {
        prod[i] = co_create(65536, producer, &pa[i]);
        cons[i] = co_create(65536, consumer, &ca[i]);
    }

    for (int i = 0; i < 2; i++) {
        co_spawn(prod[i]);
        co_spawn(cons[i]);
    }

    co_run();

    for (int i = 0; i < 2; i++) {
        co_destroy(prod[i]);
        co_destroy(cons[i]);
    }

    puts("=== all done ===");
    return 0;
}

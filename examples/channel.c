/*
 * examples/channel.c
 *
 * A bounded, single-producer / single-consumer channel built on top of the
 * coroutine scheduler. Demonstrates how to block a sender when the buffer is
 * full and a receiver when the buffer is empty, using co_yield + a ready
 * flag instead of OS-level primitives.
 *
 * Build:
 *     make examples
 *     ./build/examples/channel
 *
 * Expected output:
 *     producer: sent 0
 *     consumer: got 0
 *     producer: sent 1
 *     consumer: got 1
 *     ...
 *     producer: done
 *     consumer: done
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coroutine.h"

#define CHAN_CAP 4
#define N_ITEMS  16

typedef struct {
    int     buf[CHAN_CAP];
    size_t  head;       /* next index to read  */
    size_t  tail;       /* next index to write */
    size_t  size;       /* items currently in buffer */
    co_t   *sender;     /* coroutine parked waiting to send   */
    co_t   *receiver;   /* coroutine parked waiting to receive */
    int     closed;
} chan_t;

static void chan_init(chan_t *c) {
    memset(c, 0, sizeof(*c));
}

static void chan_send(chan_t *c, int v) {
    while (c->size == CHAN_CAP && !c->closed) {
        c->sender = co_self();
        co_yield();
    }
    if (c->closed) {
        fprintf(stderr, "chan_send on closed channel\n");
        abort();
    }
    c->buf[c->tail] = v;
    c->tail = (c->tail + 1) % CHAN_CAP;
    c->size++;
    if (c->receiver) {
        co_t *w = c->receiver;
        c->receiver = NULL;
        co_wake(w);
    }
}

static int chan_recv(chan_t *c, int *out) {
    while (c->size == 0 && !c->closed) {
        c->receiver = co_self();
        co_yield();
    }
    if (c->size == 0 && c->closed) {
        return 0; /* channel drained */
    }
    *out = c->buf[c->head];
    c->head = (c->head + 1) % CHAN_CAP;
    c->size--;
    if (c->sender) {
        co_t *w = c->sender;
        c->sender = NULL;
        co_wake(w);
    }
    return 1;
}

static void chan_close(chan_t *c) {
    c->closed = 1;
    if (c->receiver) {
        co_t *w = c->receiver;
        c->receiver = NULL;
        co_wake(w);
    }
}

/* --- coroutines ------------------------------------------------------- */

static void producer(void *arg) {
    chan_t *c = arg;
    for (int i = 0; i < N_ITEMS; i++) {
        chan_send(c, i);
        printf("producer: sent %d\n", i);
    }
    chan_close(c);
    printf("producer: done\n");
}

static void consumer(void *arg) {
    chan_t *c = arg;
    int v;
    while (chan_recv(c, &v)) {
        printf("consumer: got %d\n", v);
    }
    printf("consumer: done\n");
}

int main(void) {
    chan_t c;
    chan_init(&c);

    co_spawn(producer, &c, 0 /* default stack */);
    co_spawn(consumer, &c, 0);

    co_run(); /* drives the scheduler until all coroutines retire */
    return 0;
}

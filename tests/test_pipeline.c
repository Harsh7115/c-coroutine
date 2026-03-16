/*
 * test_pipeline.c — producer / filter / consumer pipeline
 *
 * producer  →  [buffer]  →  squarer  →  [buffer]  →  consumer
 *
 * The producer emits integers 1..N.
 * The squarer reads each integer and writes its square.
 * The consumer reads each square and accumulates a sum.
 */

#include "../include/coroutine.h"

#include <assert.h>
#include <stdio.h>

#define N 20

#define RING_CAP 4

typedef struct {
    int  buf[RING_CAP];
    int  head, tail, size;
    int  done;
} Ring;

static void ring_init(Ring *r) {
    r->head = r->tail = r->size = r->done = 0;
}

static int ring_full(const Ring *r)  { return r->size == RING_CAP; }
static int ring_empty(const Ring *r) { return r->size == 0; }

static void ring_push(Ring *r, int v) {
    assert(!ring_full(r));
    r->buf[r->tail] = v;
    r->tail = (r->tail + 1) % RING_CAP;
    r->size++;
}

static int ring_pop(Ring *r) {
    assert(!ring_empty(r));
    int v = r->buf[r->head];
    r->head = (r->head + 1) % RING_CAP;
    r->size--;
    return v;
}

typedef struct { Ring *out; }         ProducerArgs;
typedef struct { Ring *in; Ring *out; } SquarerArgs;
typedef struct { Ring *in; long sum; } ConsumerArgs;

static void producer(void *arg) {
    Ring *out = ((ProducerArgs *)arg)->out;
    for (int i = 1; i <= N; i++) {
        while (ring_full(out))   co_yield();
        ring_push(out, i);
        co_yield();
    }
    out->done = 1;
}

static void squarer(void *arg) {
    Ring *in  = ((SquarerArgs *)arg)->in;
    Ring *out = ((SquarerArgs *)arg)->out;
    for (;;) {
        while (ring_empty(in)) {
            if (in->done) { out->done = 1; return; }
            co_yield();
        }
        int v = ring_pop(in);
        while (ring_full(out)) co_yield();
        ring_push(out, v * v);
        co_yield();
    }
}

static void consumer(void *arg) {
    ConsumerArgs *a = (ConsumerArgs *)arg;
    for (;;) {
        while (ring_empty(a->in)) {
            if (a->in->done) return;
            co_yield();
        }
        a->sum += ring_pop(a->in);
        co_yield();
    }
}

int main(void) {
    fprintf(stderr, "=== test_pipeline ===\n");

    Ring r1, r2;
    ring_init(&r1);
    ring_init(&r2);

    ProducerArgs pa = { .out = &r1 };
    SquarerArgs  sa = { .in  = &r1, .out = &r2 };
    ConsumerArgs ca = { .in  = &r2, .sum = 0 };

    Coroutine *cp = co_create(producer, &pa, 0);
    Coroutine *cs = co_create(squarer,  &sa, 0);
    Coroutine *cc = co_create(consumer, &ca, 0);

    co_run();

    long expected = (long)N * (N + 1) * (2 * N + 1) / 6;
    int  ok       = (ca.sum == expected);

    fprintf(stderr, "  sum of squares 1..%d = %ld (expected %ld) — %s\n",
            N, ca.sum, expected, ok ? "PASS" : "FAIL");

    co_free(cp);
    co_free(cs);
    co_free(cc);

    return ok ? 0 : 1;
}

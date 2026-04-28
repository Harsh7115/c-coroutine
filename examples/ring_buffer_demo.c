/*
 * examples/ring_buffer_demo.c
 *
 * A bounded-buffer producer/consumer example built on top of the
 * cooperative coroutine library. The producer pushes integers into a
 * shared ring buffer; the consumer pops and prints them. Both yield
 * to the scheduler whenever the buffer is full or empty.
 *
 * Build (from the repo root):
 *   cc -Iinclude -o ring_buffer_demo examples/ring_buffer_demo.c src/coroutine.c src/scheduler.c
 *   ./ring_buffer_demo
 *
 * Expected output: integers 0..19 produced and consumed in order,
 * with the producer and consumer interleaving as the buffer fills/drains.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coroutine.h"
#include "scheduler.h"

#define BUFFER_CAPACITY  4
#define ITEM_COUNT       20

typedef struct ring_buffer {
    int  data[BUFFER_CAPACITY];
    int  head;
    int  tail;
    int  count;
} ring_buffer;

static int rb_push(ring_buffer *rb, int value) {
    if (rb->count == BUFFER_CAPACITY) {
        return -1;
    }
    rb->data[rb->tail] = value;
    rb->tail = (rb->tail + 1) % BUFFER_CAPACITY;
    rb->count++;
    return 0;
}

static int rb_pop(ring_buffer *rb, int *out) {
    if (rb->count == 0) {
        return -1;
    }
    *out = rb->data[rb->head];
    rb->head = (rb->head + 1) % BUFFER_CAPACITY;
    rb->count--;
    return 0;
}

static void producer(void *arg) {
    ring_buffer *rb = (ring_buffer *)arg;
    for (int i = 0; i < ITEM_COUNT; i++) {
        while (rb_push(rb, i) != 0) {
            /* buffer full — hand control back to the scheduler */
            co_yield();
        }
        printf("[producer] produced %d (count=%d)
", i, rb->count);
        co_yield();
    }
}

static void consumer(void *arg) {
    ring_buffer *rb = (ring_buffer *)arg;
    int received = 0;
    while (received < ITEM_COUNT) {
        int value = 0;
        while (rb_pop(rb, &value) != 0) {
            /* buffer empty — hand control back to the scheduler */
            co_yield();
        }
        printf("[consumer] consumed %d (count=%d)
", value, rb->count);
        received++;
        co_yield();
    }
}

int main(void) {
    ring_buffer rb;
    memset(&rb, 0, sizeof(rb));

    scheduler_init();

    coroutine_t *p = co_create(producer, &rb, /*stack_size*/ 16 * 1024);
    coroutine_t *c = co_create(consumer, &rb, /*stack_size*/ 16 * 1024);
    if (!p || !c) {
        fprintf(stderr, "failed to create coroutines
");
        return EXIT_FAILURE;
    }

    scheduler_run();

    co_destroy(p);
    co_destroy(c);
    scheduler_shutdown();
    return EXIT_SUCCESS;
}

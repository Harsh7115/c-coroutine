/*
 * test_channel.c
 *
 * Smoke tests for the buffered channel built on top of c-coroutine.
 * The channel under test is the one demonstrated in examples/channel.c:
 * a fixed-capacity ring buffer guarded by two wait queues (one for
 * senders blocked on full, one for receivers blocked on empty).
 *
 * These tests are intentionally single-threaded and deterministic --
 * the cooperative scheduler is what makes that reasonable.
 */

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "co.h"
#include "co_channel.h"

#define CAP 4

static co_channel_t *ch;
static int produced[16];
static int consumed[16];
static size_t produced_n;
static size_t consumed_n;

static void producer(void *arg) {
    int n = (int)(intptr_t)arg;
    for (int i = 0; i < n; i++) {
        int value = i * 10;
        produced[produced_n++] = value;
        co_channel_send(ch, &value, sizeof(value));
    }
    co_channel_close(ch);
}

static void consumer(void *arg) {
    (void)arg;
    int value;
    while (co_channel_recv(ch, &value, sizeof(value)) == 0) {
        consumed[consumed_n++] = value;
    }
}

static void test_send_then_recv(void) {
    ch = co_channel_new(sizeof(int), CAP);
    produced_n = consumed_n = 0;

    co_spawn(producer, (void *)(intptr_t)8);
    co_spawn(consumer, NULL);
    co_run();

    assert(produced_n == 8);
    assert(consumed_n == 8);
    for (size_t i = 0; i < produced_n; i++) {
        assert(produced[i] == consumed[i]);
    }
    co_channel_free(ch);
    printf("  ok  send_then_recv\n");
}

static void test_blocks_when_full(void) {
    ch = co_channel_new(sizeof(int), 2);
    int sentinel = 99;

    /* Fill the channel without any receiver. The producer should park
     * after the second send, allowing the test to observe both values
     * already buffered. */
    for (int i = 0; i < 2; i++) {
        co_channel_send(ch, &sentinel, sizeof(sentinel));
    }
    assert(co_channel_len(ch) == 2);
    assert(co_channel_is_full(ch));

    int got = 0;
    co_channel_recv(ch, &got, sizeof(got));
    assert(got == sentinel);
    assert(!co_channel_is_full(ch));
    co_channel_free(ch);
    printf("  ok  blocks_when_full\n");
}

static void test_close_unblocks_recv(void) {
    ch = co_channel_new(sizeof(int), CAP);
    consumed_n = 0;

    co_spawn(consumer, NULL);
    co_yield();           /* let the consumer block on empty */
    co_channel_close(ch); /* should wake the consumer with EOF */
    co_run();

    assert(consumed_n == 0);
    co_channel_free(ch);
    printf("  ok  close_unblocks_recv\n");
}

int main(void) {
    printf("test_channel\n");
    test_send_then_recv();
    test_blocks_when_full();
    test_close_unblocks_recv();
    printf("all channel tests passed\n");
    return 0;
}

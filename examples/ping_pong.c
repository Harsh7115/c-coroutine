/*
 * examples/ping_pong.c
 *
 * Two coroutines bouncing a counter back and forth via co_yield().
 *
 * Demonstrates:
 *   - cooperative scheduling between exactly two coroutines
 *   - shared state without locks (single-threaded by design)
 *   - clean termination via a stop sentinel
 *
 * Build:
 *   gcc -Iinclude examples/ping_pong.c lib/libcoroutine.a -o ping_pong
 *
 * Run:
 *   ./ping_pong 10
 *
 * Expected output (for N=4):
 *   [ping] 1
 *   [pong] 2
 *   [ping] 3
 *   [pong] 4
 *   ping_pong: done after 4 exchanges
 */

#include "include/coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared state. Safe to read/write without atomics or mutexes because
 * the coroutines are single-threaded and only swap when one of them
 * voluntarily calls co_yield(). */
typedef struct {
    int counter;
    int limit;
    int done;
} pp_state;

static void ping(void *arg) {
    pp_state *st = (pp_state *)arg;
    while (st->counter < st->limit) {
        st->counter += 1;
        printf("[ping] %d
", st->counter);
        co_yield();
    }
    st->done = 1;
}

static void pong(void *arg) {
    pp_state *st = (pp_state *)arg;
    /* pong waits for ping to bump the counter first, then prints. */
    while (st->counter < st->limit && !st->done) {
        co_yield();
        if (st->counter >= st->limit) break;
        st->counter += 1;
        printf("[pong] %d
", st->counter);
    }
}

static int parse_limit(int argc, char **argv, int fallback) {
    if (argc < 2) return fallback;
    char *end = NULL;
    long v = strtol(argv[1], &end, 10);
    if (!end || *end != '\0' || v <= 0 || v > 1000000) {
        fprintf(stderr, "ping_pong: invalid limit '%s', using %d
",
                argv[1], fallback);
        return fallback;
    }
    return (int)v;
}

int main(int argc, char **argv) {
    pp_state st;
    memset(&st, 0, sizeof st);
    st.limit = parse_limit(argc, argv, 10);

    Coroutine *p1 = co_create(ping, &st, 0);
    Coroutine *p2 = co_create(pong, &st, 0);
    if (!p1 || !p2) {
        fprintf(stderr, "ping_pong: co_create failed
");
        return 1;
    }

    co_run();

    printf("ping_pong: done after %d exchanges
", st.counter);

    co_free(p1);
    co_free(p2);
    return 0;
}

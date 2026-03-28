/*
 * echo_server.c — cooperative echo server simulation using c-coroutine
 *
 * Demonstrates how to use the coroutine library to handle multiple
 * "connections" concurrently on a single thread without blocking I/O.
 * Each connection is modelled as a coroutine that reads a message,
 * echoes it back with a sequence number, then yields so other
 * connections can make progress.
 *
 * Build:
 *   gcc -Iinclude examples/echo_server.c lib/libcoroutine.a -o echo_server
 * Run:
 *   ./echo_server
 */

#include "coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MESSAGES  5   /* how many messages each connection sends   */
#define NUM_CONNS     4   /* number of simulated concurrent connections */

/* --------------------------------------------------------------------------
 * Connection state
 * -------------------------------------------------------------------------- */
typedef struct {
    int   id;                          /* connection ID                   */
    int   seq;                         /* next sequence number to send    */
    char  recv_buf[64];                /* simulated receive buffer        */
} Conn;

/* Simulated "network read": just formats a message into the conn buffer.  */
static void sim_read(Conn *c) {
    snprintf(c->recv_buf, sizeof(c->recv_buf),
             "hello from conn %d, msg %d", c->id, c->seq);
}

/* Simulated "network write": prints the echoed message to stdout.         */
static void sim_write(const Conn *c, const char *data) {
    printf("[conn %d] echo(%d): %s\n", c->id, c->seq, data);
}

/* --------------------------------------------------------------------------
 * Per-connection coroutine body
 * -------------------------------------------------------------------------- */
static void handle_connection(void *arg) {
    Conn *c = (Conn *)arg;

    printf("[conn %d] accepted\n", c->id);

    for (c->seq = 1; c->seq <= MAX_MESSAGES; c->seq++) {
        /* Simulate waiting for data from the network. In a real async
         * design this would yield until the fd is readable; here we
         * just yield once to let other coroutines run.               */
        co_yield();

        sim_read(c);
        sim_write(c, c->recv_buf);

        /* Yield again to simulate the time it takes to flush output. */
        co_yield();
    }

    printf("[conn %d] closed after %d messages\n", c->id, MAX_MESSAGES);
}

/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */
int main(void) {
    Conn       conns[NUM_CONNS];
    Coroutine *cos[NUM_CONNS];

    printf("=== cooperative echo server (%d connections) ===\n\n",
           NUM_CONNS);

    /* Spawn one coroutine per connection. */
    for (int i = 0; i < NUM_CONNS; i++) {
        conns[i].id  = i + 1;
        conns[i].seq = 0;
        memset(conns[i].recv_buf, 0, sizeof(conns[i].recv_buf));

        cos[i] = co_create(handle_connection, &conns[i], 0);
        if (!cos[i]) {
            fprintf(stderr, "co_create failed for conn %d\n", i + 1);
            return 1;
        }
    }

    /* Hand control to the scheduler.  Returns when all coroutines finish. */
    co_run();

    /* Clean up. */
    for (int i = 0; i < NUM_CONNS; i++) {
        co_free(cos[i]);
    }

    printf("\nAll connections closed. Server done.\n");
    return 0;
}

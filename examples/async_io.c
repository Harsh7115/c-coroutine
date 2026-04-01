/*
 * async_io.c — Simulated async I/O with coroutines
 *
 * Demonstrates how cooperative coroutines can overlap "I/O" operations
 * without threads or callbacks.  Each worker coroutine issues a fake
 * read or write, yields control back to the scheduler while the operation
 * is "in flight", and resumes when it completes.
 *
 * Build:
 *   gcc -O2 -I../include async_io.c ../src/coroutine.c -o async_io
 *
 * Expected output (order may vary):
 *   [io-worker-0] starting read  (fd=3, 64 bytes)
 *   [io-worker-1] starting read  (fd=4, 128 bytes)
 *   [io-worker-2] starting write (fd=5, 32 bytes)
 *   [io-worker-0] read  complete (64 bytes in ~2 ticks)
 *   [io-worker-1] read  complete (128 bytes in ~3 ticks)
 *   [io-worker-2] write complete (32 bytes in ~1 tick)
 *   all workers done – total ticks: 3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coroutine.h"

/* ---------- fake async I/O layer ---------- */

#define MAX_OPS 16

typedef enum { OP_READ, OP_WRITE } io_op_t;

typedef struct {
    int       active;        /* 1 while in-flight                  */
    io_op_t   kind;
    int       fd;
    size_t    nbytes;
    int       ticks_left;   /* simulated latency                   */
    int       worker_id;
} io_request_t;

static io_request_t io_queue[MAX_OPS];
static int          global_tick = 0;

/* Submit a fake I/O request; returns slot index. */
static int io_submit(io_op_t kind, int fd, size_t nbytes, int latency, int wid)
{
    for (int i = 0; i < MAX_OPS; i++) {
        if (!io_queue[i].active) {
            io_queue[i].active     = 1;
            io_queue[i].kind       = kind;
            io_queue[i].fd         = fd;
            io_queue[i].nbytes     = nbytes;
            io_queue[i].ticks_left = latency;
            io_queue[i].worker_id  = wid;
            return i;
        }
    }
    return -1; /* queue full */
}

/* Advance all in-flight requests by one tick; return number still active. */
static int io_tick(void)
{
    int still_active = 0;
    for (int i = 0; i < MAX_OPS; i++) {
        if (io_queue[i].active) {
            io_queue[i].ticks_left--;
            if (io_queue[i].ticks_left <= 0)
                io_queue[i].active = 0; /* completed */
            else
                still_active++;
        }
    }
    global_tick++;
    return still_active;
}

/* Return 1 if the I/O request in slot idx is done. */
static int io_done(int slot)
{
    return !io_queue[slot].active;
}

/* ---------- worker coroutine ---------- */

typedef struct {
    int    id;
    io_op_t kind;
    int    fd;
    size_t  nbytes;
    int    latency;   /* simulated ticks to complete */
} worker_args_t;

static void io_worker(void *arg)
{
    worker_args_t *a = (worker_args_t *)arg;
    const char *opname = (a->kind == OP_READ) ? "read " : "write";

    printf("[io-worker-%d] starting %s (fd=%d, %zu bytes)\n",
           a->id, opname, a->fd, a->nbytes);

    int slot = io_submit(a->kind, a->fd, a->nbytes, a->latency, a->id);
    if (slot < 0) {
        fprintf(stderr, "[io-worker-%d] ERROR: I/O queue full\n", a->id);
        return;
    }

    int start_tick = global_tick;

    /* Yield until the I/O completes — zero CPU wasted while waiting. */
    while (!io_done(slot))
        co_yield();

    int elapsed = global_tick - start_tick;
    printf("[io-worker-%d] %s complete (%zu bytes in ~%d tick%s)\n",
           a->id, opname, a->nbytes, elapsed, elapsed == 1 ? "" : "s");
}

/* ---------- main / event loop ---------- */

#define NUM_WORKERS 3

int main(void)
{
    memset(io_queue, 0, sizeof(io_queue));

    /* Describe each worker's workload */
    worker_args_t args[NUM_WORKERS] = {
        { .id = 0, .kind = OP_READ,  .fd = 3, .nbytes = 64,  .latency = 2 },
        { .id = 1, .kind = OP_READ,  .fd = 4, .nbytes = 128, .latency = 3 },
        { .id = 2, .kind = OP_WRITE, .fd = 5, .nbytes = 32,  .latency = 1 },
    };

    coroutine_t workers[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++)
        workers[i] = co_create(io_worker, &args[i]);

    /*
     * Simple event loop:
     *   1. Run all coroutines until they yield.
     *   2. Advance the fake I/O clock by one tick.
     *   3. Repeat until all coroutines have finished.
     */
    int active;
    do {
        active = 0;
        for (int i = 0; i < NUM_WORKERS; i++) {
            if (co_status(workers[i]) != CO_DEAD) {
                co_resume(workers[i]);
                if (co_status(workers[i]) != CO_DEAD)
                    active++;
            }
        }
        if (active > 0)
            io_tick();
    } while (active > 0);

    printf("all workers done – total ticks: %d\n", global_tick);

    for (int i = 0; i < NUM_WORKERS; i++)
        co_destroy(workers[i]);

    return 0;
}

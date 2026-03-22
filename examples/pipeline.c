/*
 * pipeline.c - 3-stage coroutine pipeline example
 *
 * Demonstrates how c-coroutine can model a Unix-style pipeline:
 *
 *   source  -->  transform  -->  sink
 *
 * Each stage is a coroutine. Data flows through the pipeline via
 * co_yield / co_await, so the stages interleave cooperatively on a
 * single thread - no threads, no mutexes, no OS scheduling overhead.
 *
 * Compile (assuming c-coroutine is installed):
 *   gcc -O2 -Wall -Wextra -I../include pipeline.c -L.. -lcoroutine -o pipeline
 *
 * Expected output (integers 0-9, doubled, with a running sum):
 *   [source]    emit 0
 *   [transform] 0 -> 0
 *   [sink]      received 0   (total = 0)
 *   [source]    emit 1
 *   [transform] 1 -> 2
 *   [sink]      received 2   (total = 2)
 *   ...
 *   Done. Final sum = 90
 */

#include <stdio.h>
#include <stdlib.h>
#include "coroutine.h"

/* ---------- shared channel structure ---------------------------------- */

/*
 * A tiny one-slot channel: producer sets .value and marks .ready = 1;
 * consumer clears .ready after reading.  Both sides co_yield while
 * waiting, so the scheduler can make progress elsewhere.
 */
typedef struct {
    int value;
    int ready; /* 1 = data available, 0 = slot empty */
} Channel;

/* ---------- coroutine contexts ---------------------------------------- */

typedef struct {
    Channel *out; /* channel toward the next stage */
    int      n;   /* how many integers to emit      */
} SourceCtx;

typedef struct {
    Channel *in;
    Channel *out;
} TransformCtx;

typedef struct {
    Channel *in;
    int      total; /* running sum, written back to main */
} SinkCtx;

/* ---------- stage implementations ------------------------------------- */

/*
 * source: emits integers 0 .. ctx->n-1 down the output channel.
 */
static void source_fn(void *arg)
{
    SourceCtx *ctx = (SourceCtx *)arg;

    for (int i = 0; i < ctx->n; i++) {
        /* wait until the slot is free */
        while (ctx->out->ready)
            co_yield();

        printf("[source]    emit %d\n", i);
        ctx->out->value = i;
        ctx->out->ready = 1;
        co_yield();
    }

    /* sentinel: negative value signals EOF */
    while (ctx->out->ready)
        co_yield();
    ctx->out->value = -1;
    ctx->out->ready  = 1;
}

/*
 * transform: doubles every integer it reads from 'in' and forwards it
 * to 'out'.  Stops when it sees the EOF sentinel (-1).
 */
static void transform_fn(void *arg)
{
    TransformCtx *ctx = (TransformCtx *)arg;

    for (;;) {
        /* wait for input */
        while (!ctx->in->ready)
            co_yield();

        int val = ctx->in->value;
        ctx->in->ready = 0; /* consume the slot */

        if (val < 0) {
            /* propagate EOF downstream */
            while (ctx->out->ready)
                co_yield();
            ctx->out->value = -1;
            ctx->out->ready  = 1;
            return;
        }

        printf("[transform] %d -> %d\n", val, val * 2);

        /* wait until the output slot is free */
        while (ctx->out->ready)
            co_yield();
        ctx->out->value = val * 2;
        ctx->out->ready  = 1;
        co_yield();
    }
}

/*
 * sink: accumulates a running sum of every integer it receives.
 * Stops on EOF sentinel.
 */
static void sink_fn(void *arg)
{
    SinkCtx *ctx = (SinkCtx *)arg;
    ctx->total = 0;

    for (;;) {
        while (!ctx->in->ready)
            co_yield();

        int val = ctx->in->value;
        ctx->in->ready = 0;

        if (val < 0)
            return; /* EOF */

        ctx->total += val;
        printf("[sink]      received %d   (total = %d)\n", val, ctx->total);
        co_yield();
    }
}

/* ---------- main ------------------------------------------------------ */

int main(void)
{
    /* two channels connect the three stages */
    Channel src_to_xfm = {0, 0};
    Channel xfm_to_snk = {0, 0};

    /* contexts */
    SourceCtx    sctx = { .out = &src_to_xfm, .n = 10 };
    TransformCtx tctx = { .in  = &src_to_xfm, .out = &xfm_to_snk };
    SinkCtx      kctx = { .in  = &xfm_to_snk };

    /* spawn the three coroutines */
    co_spawn(source_fn,    &sctx, 65536);
    co_spawn(transform_fn, &tctx, 65536);
    co_spawn(sink_fn,      &kctx, 65536);

    /* run the scheduler until all coroutines finish */
    co_run();

    printf("\nDone. Final sum = %d\n", kctx.total);
    /* sum of 0*2 + 1*2 + ... + 9*2 = 2*(0+1+...+9) = 2*45 = 90 */
    return 0;
}

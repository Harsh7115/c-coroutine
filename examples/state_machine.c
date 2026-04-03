/*
 * state_machine.c — Finite State Machine (FSM) via coroutines
 *
 * Demonstrates how to encode each FSM state as a suspended coroutine.
 * Transitions are modelled by co_yield()ing back to the scheduler, which
 * dispatches the next-state coroutine based on the event queue.
 *
 * FSM modelled here: a simple TCP-like connection handshake
 *
 *   CLOSED ──SYN──► SYN_SENT ──SYN_ACK──► ESTABLISHED ──FIN──► CLOSING ──► CLOSED
 *
 * Each state is implemented as a coroutine.  An event pump coroutine reads
 * events from a ring buffer and wakes the appropriate state coroutine.
 *
 * Compile (from repo root):
 *   gcc -Iinclude -o state_machine examples/state_machine.c src/coroutine.c src/scheduler.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coroutine.h"

/* ── event types ────────────────────────────────────────────────────── */

typedef enum {
    EVT_NONE = 0,
    EVT_SYN,
    EVT_SYN_ACK,
    EVT_FIN,
    EVT_RESET,
    EVT_DATA,
} Event;

static const char *event_name(Event e) {
    switch (e) {
        case EVT_NONE:    return "NONE";
        case EVT_SYN:     return "SYN";
        case EVT_SYN_ACK: return "SYN_ACK";
        case EVT_FIN:     return "FIN";
        case EVT_RESET:   return "RESET";
        case EVT_DATA:    return "DATA";
        default:          return "UNKNOWN";
    }
}

/* ── shared state ───────────────────────────────────────────────────── */

#define QUEUE_CAP 16

typedef struct {
    Event   buf[QUEUE_CAP];
    int     head, tail, len;
} EventQueue;

static EventQueue g_queue;

static void queue_push(EventQueue *q, Event e) {
    if (q->len == QUEUE_CAP) {
        fprintf(stderr, "event queue overflow\n");
        return;
    }
    q->buf[q->tail] = e;
    q->tail = (q->tail + 1) % QUEUE_CAP;
    q->len++;
}

static Event queue_pop(EventQueue *q) {
    if (q->len == 0) return EVT_NONE;
    Event e = q->buf[q->head];
    q->head = (q->head + 1) % QUEUE_CAP;
    q->len--;
    return e;
}

/* ── FSM state coroutines ───────────────────────────────────────────── */

/* Each state coroutine:
 *   1. Prints that we entered the state.
 *   2. co_yield()s, giving control back to the event pump.
 *   3. Wakes up when the pump dispatches an event.
 *   4. Decides on the next state and pushes a synthetic event, then yields.
 */

static coroutine_t *co_syn_sent;
static coroutine_t *co_established;
static coroutine_t *co_closing;
static coroutine_t *co_pump;      /* event pump */

static int g_bytes_transferred = 0;

/* SYN_SENT state — waiting for SYN_ACK */
static void state_syn_sent(void *arg) {
    (void)arg;
    while (1) {
        printf("[SYN_SENT]  waiting for SYN_ACK...\n");
        co_yield();

        Event e = queue_pop(&g_queue);
        printf("[SYN_SENT]  got event: %s\n", event_name(e));

        if (e == EVT_SYN_ACK) {
            printf("[SYN_SENT]  → transitioning to ESTABLISHED\n");
            /* wake established state */
            queue_push(&g_queue, EVT_DATA); /* handshake done, send data */
            co_resume(co_established);
            return;
        } else if (e == EVT_RESET) {
            printf("[SYN_SENT]  → connection reset, back to CLOSED\n");
            return;
        } else {
            printf("[SYN_SENT]  unexpected event, staying\n");
        }
    }
}

/* ESTABLISHED state — data transfer phase */
static void state_established(void *arg) {
    (void)arg;
    while (1) {
        printf("[ESTABLISHED] connection up, transferring data...\n");
        co_yield();

        Event e = queue_pop(&g_queue);
        printf("[ESTABLISHED] got event: %s\n", event_name(e));

        if (e == EVT_DATA) {
            g_bytes_transferred += 512;
            printf("[ESTABLISHED] transferred 512 B (total %d B)\n",
                   g_bytes_transferred);
            if (g_bytes_transferred >= 1024) {
                printf("[ESTABLISHED] transfer complete, initiating close\n");
                queue_push(&g_queue, EVT_FIN);
                co_resume(co_closing);
                return;
            }
            /* more data to transfer */
            queue_push(&g_queue, EVT_DATA);
        } else if (e == EVT_FIN) {
            printf("[ESTABLISHED] → peer closed, transitioning to CLOSING\n");
            co_resume(co_closing);
            return;
        } else if (e == EVT_RESET) {
            printf("[ESTABLISHED] → reset, going to CLOSED\n");
            return;
        }
    }
}

/* CLOSING state — draining and cleanup */
static void state_closing(void *arg) {
    (void)arg;
    printf("[CLOSING]   draining buffers and sending FIN_ACK...\n");
    co_yield();

    printf("[CLOSING]   → connection fully closed (CLOSED)\n");
}

/* Event pump — feeds events from g_queue to the right state coroutine */
static void event_pump(void *arg) {
    (void)arg;

    /* Seed the FSM: initiate connection */
    printf("[PUMP]      initiating connection (SYN)\n");
    queue_push(&g_queue, EVT_SYN);

    /* Kick off SYN_SENT state */
    co_resume(co_syn_sent);

    /* Simulate receiving SYN_ACK from peer */
    printf("[PUMP]      peer acknowledged SYN → sending SYN_ACK\n");
    queue_push(&g_queue, EVT_SYN_ACK);
    co_resume(co_syn_sent);

    /* The established / closing states are driven by data events above */
    co_resume(co_established);
    co_resume(co_closing);

    printf("[PUMP]      FSM run complete. bytes=%d\n", g_bytes_transferred);
}

/* ── main ───────────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Coroutine-based FSM: TCP-like handshake demo ===\n\n");

    memset(&g_queue, 0, sizeof(g_queue));

    /* Create state coroutines (not yet started) */
    co_syn_sent    = co_create(state_syn_sent,    NULL, 64 * 1024);
    co_established = co_create(state_established, NULL, 64 * 1024);
    co_closing     = co_create(state_closing,     NULL, 64 * 1024);
    co_pump        = co_create(event_pump,         NULL, 64 * 1024);

    if (!co_syn_sent || !co_established || !co_closing || !co_pump) {
        fprintf(stderr, "co_create failed\n");
        return 1;
    }

    /* Run the pump, which drives the whole FSM */
    co_run(co_pump);

    /* Cleanup */
    co_destroy(co_syn_sent);
    co_destroy(co_established);
    co_destroy(co_closing);
    co_destroy(co_pump);

    printf("\n=== done ===\n");
    return 0;
}

/**
 * timer_wheel.c — Hierarchical timer wheel using c-coroutine
 *
 * Demonstrates a software timer wheel: a time-keeping coroutine advances
 * the wheel on each tick, firing callbacks for expired timers.  All timer
 * callbacks run as lightweight coroutines, so they can co_yield() without
 * blocking the wheel.
 *
 * Build:
 *   gcc -O2 -o timer_wheel timer_wheel.c -I../include -L../ -lcoroutine
 * Run:
 *   ./timer_wheel
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coroutine.h"

/* ------------------------------------------------------------------ */
/* Timer wheel configuration                                            */
/* ------------------------------------------------------------------ */

#define WHEEL_SLOTS   16          /* must be a power of 2              */
#define WHEEL_MASK    (WHEEL_SLOTS - 1)
#define MAX_TIMERS    64

/* ------------------------------------------------------------------ */
/* Timer entry                                                          */
/* ------------------------------------------------------------------ */

typedef void (*timer_cb_t)(void *arg);

typedef struct timer_entry {
    timer_cb_t        cb;
    void             *arg;
    unsigned int      remaining_laps;  /* # full wheel revolutions left */
    int               active;
    struct timer_entry *next;           /* intrusive linked list per slot */
} timer_entry_t;

/* ------------------------------------------------------------------ */
/* Wheel state                                                          */
/* ------------------------------------------------------------------ */

static timer_entry_t  g_pool[MAX_TIMERS];
static timer_entry_t *g_wheel[WHEEL_SLOTS];
static int            g_current_tick = 0;

static void wheel_init(void)
{
    memset(g_pool,  0, sizeof(g_pool));
    memset(g_wheel, 0, sizeof(g_wheel));
}

static timer_entry_t *timer_alloc(void)
{
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!g_pool[i].active) {
            g_pool[i].active = 1;
            return &g_pool[i];
        }
    }
    return NULL;   /* pool exhausted */
}

/**
 * add_timer - schedule a one-shot callback after `delay` ticks.
 */
static timer_entry_t *add_timer(unsigned int delay, timer_cb_t cb, void *arg)
{
    timer_entry_t *t = timer_alloc();
    if (!t) { fprintf(stderr, "timer pool exhausted\n"); return NULL; }

    unsigned int slot          = (g_current_tick + delay) & WHEEL_MASK;
    t->remaining_laps          = delay / WHEEL_SLOTS;
    t->cb                      = cb;
    t->arg                     = arg;

    /* prepend to slot list */
    t->next       = g_wheel[slot];
    g_wheel[slot] = t;
    return t;
}

/**
 * wheel_tick - advance the wheel by one tick and fire expired timers.
 * Each callback is launched as an independent coroutine.
 */
static void wheel_tick(void)
{
    g_current_tick++;
    int slot = g_current_tick & WHEEL_MASK;

    timer_entry_t *prev = NULL;
    timer_entry_t *t    = g_wheel[slot];

    while (t) {
        timer_entry_t *next = t->next;

        if (t->remaining_laps > 0) {
            t->remaining_laps--;
            prev = t;
        } else {
            /* detach from list */
            if (prev) prev->next = next;
            else       g_wheel[slot] = next;

            /* fire callback as a coroutine */
            coroutine_t *c = co_create(t->cb, t->arg, 32 * 1024);
            if (c) {
                co_schedule(c);
                co_run();
                co_destroy(c);
            }

            t->active = 0;
        }
        t = next;
    }
}

/* ------------------------------------------------------------------ */
/* Example timer callbacks (each is a coroutine)                        */
/* ------------------------------------------------------------------ */

static void heartbeat_cb(void *arg)
{
    int id = *(int *)arg;
    printf("[tick %3d] heartbeat-%d fired\n", g_current_tick, id);
    co_yield();   /* simulate async work */
    printf("[tick %3d] heartbeat-%d done\n",  g_current_tick, id);
}

static void watchdog_cb(void *arg)
{
    (void)arg;
    printf("[tick %3d] WATCHDOG fired — system healthy\n", g_current_tick);
}

static void delayed_print_cb(void *arg)
{
    const char *msg = (const char *)arg;
    printf("[tick %3d] delayed message: \"%s\"\n", g_current_tick, msg);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    wheel_init();

    printf("=== Timer Wheel demo (%d slots) ===\n\n", WHEEL_SLOTS);

    /* Schedule several timers */
    int id1 = 1, id2 = 2, id3 = 3;
    add_timer( 3, heartbeat_cb,    &id1);
    add_timer( 5, heartbeat_cb,    &id2);
    add_timer( 8, heartbeat_cb,    &id3);
    add_timer( 4, watchdog_cb,     NULL);
    add_timer(10, watchdog_cb,     NULL);
    add_timer( 7, delayed_print_cb, (void *)"hello from the future");
    add_timer(12, delayed_print_cb, (void *)"all timers expired");

    /* Advance the wheel for 15 ticks */
    for (int tick = 0; tick < 15; tick++) {
        printf("--- tick %d ---\n", tick + 1);
        wheel_tick();
    }

    printf("\n=== Done ===\n");
    return 0;
}

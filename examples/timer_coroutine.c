/*
 * timer_coroutine.c — Example: simulating a multi-timer system with coroutines
 *
 * Demonstrates how cooperative coroutines can implement concurrent timers
 * without threads. Each "timer" is a coroutine that counts down and yields
 * control back to the scheduler after each tick.
 *
 * Build:
 *   gcc -O2 -o timer_coroutine timer_coroutine.c -L.. -lcoroutine
 *   ./timer_coroutine
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../include/coroutine.h"

/* ------------------------------------------------------------------ */
/* Timer descriptor                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;   /* human-readable label            */
    int         ticks;  /* total ticks before firing       */
    int         period; /* if >0, auto-reload (repeating)  */
} timer_cfg_t;

/* ------------------------------------------------------------------ */
/* Coroutine body: one per timer                                        */
/* ------------------------------------------------------------------ */

static void timer_coroutine(void *arg)
{
    timer_cfg_t *cfg = (timer_cfg_t *)arg;
    int remaining = cfg->ticks;

    printf("[%s] started, fires in %d tick(s)\n", cfg->name, cfg->ticks);

    while (1) {
        while (remaining > 0) {
            printf("[%s] tick — %d remaining\n", cfg->name, remaining);
            remaining--;
            co_yield();   /* give control back to scheduler */
        }

        printf("[%s] *** FIRED ***\n", cfg->name);

        if (cfg->period > 0) {
            remaining = cfg->period;
            printf("[%s] reloaded for next %d tick(s)\n", cfg->name, remaining);
        } else {
            break;  /* one-shot: exit the coroutine */
        }
    }

    printf("[%s] done\n", cfg->name);
}

/* ------------------------------------------------------------------ */
/* Main: set up coroutines and run the scheduler                        */
/* ------------------------------------------------------------------ */

int main(void)
{
    /* Define three timers:
     *   - "Fast"  fires after 2 ticks, then repeats every 2 ticks
     *   - "Slow"  fires after 5 ticks (one-shot)
     *   - "Pulse" fires after 3 ticks, repeats every 3 ticks
     */
    timer_cfg_t timers[] = {
        { "Fast",  2, 2 },
        { "Slow",  5, 0 },
        { "Pulse", 3, 3 },
    };
    const int n = (int)(sizeof(timers) / sizeof(timers[0]));

    coroutine_t *cos[3];

    printf("=== timer_coroutine example ===\n\n");

    /* Spawn one coroutine per timer */
    for (int i = 0; i < n; i++) {
        cos[i] = co_create(timer_coroutine, &timers[i], 64 * 1024);
        if (!cos[i]) {
            fprintf(stderr, "co_create failed for timer %d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /* Run the scheduler until all coroutines finish.
     * co_run_all() returns 0 when every coroutine has returned. */
    co_run_all();

    printf("\nAll timers finished. Cleaning up.\n");

    for (int i = 0; i < n; i++) {
        co_destroy(cos[i]);
    }

    return 0;
}

/*
 * semaphore.c -- coroutine semaphore / mutex synchronization demo
 *
 * Demonstrates how to implement a counting semaphore on top of
 * c-coroutine's co_await() primitive so that coroutines block and
 * resume cooperatively without any OS primitives.
 *
 * Build:
 *   gcc -O2 -I../include -o semaphore semaphore.c ../src/coro.c
 * Run:
 *   ./semaphore
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coro.h"

/* ---------------------------------------------------------------
 * Semaphore implementation built on co_await
 * --------------------------------------------------------------- */

typedef struct {
    int count;          /* current count (>= 0)               */
    int max;            /* initial / maximum count             */
    const char *name;   /* display name for tracing            */
} coro_sem_t;

/* Initialise a semaphore with an initial count. */
static void sem_init(coro_sem_t *s, int count, const char *name) {
    s->count = count;
    s->max   = count;
    s->name  = name;
}

/* sem_wait condition: returns 1 (ready) when count > 0. */
static int sem_ready(void *arg) {
    coro_sem_t *s = (coro_sem_t *)arg;
    return s->count > 0;
}

/*
 * sem_wait -- decrement the semaphore; suspend if count == 0.
 *
 * co_await() re-schedules this coroutine only when sem_ready()
 * returns non-zero, so we never busy-spin in the scheduler.
 */
static void sem_wait(coro_sem_t *s) {
    co_await(sem_ready, s);   /* block until count > 0 */
    s->count--;
    printf("  [sem %-12s] acquired  (count now %d/%d)\n",
           s->name, s->count, s->max);
}

/* sem_post -- increment the semaphore, waking any waiting coroutines. */
static void sem_post(coro_sem_t *s) {
    if (s->count < s->max) {
        s->count++;
        printf("  [sem %-12s] released  (count now %d/%d)\n",
               s->name, s->count, s->max);
    }
}

/* ---------------------------------------------------------------
 * Shared state
 * --------------------------------------------------------------- */

#define NUM_WORKERS  5
#define NUM_SLOTS    2          /* only 2 workers may be "in critical section" */

static coro_sem_t g_sem;
static int g_shared_counter = 0;

/* ---------------------------------------------------------------
 * Worker coroutine
 * --------------------------------------------------------------- */

static void worker(void *arg) {
    int id = (int)(intptr_t)arg;

    printf("[worker %d] started\n", id);
    co_yield();   /* let all workers get scheduled before any acquires */

    /* --- enter critical section --- */
    printf("[worker %d] waiting for semaphore...\n", id);
    sem_wait(&g_sem);

    /* --- critical section (simulate work) --- */
    int before = g_shared_counter;
    co_yield();                      /* yield mid-CS to prove exclusion */
    g_shared_counter = before + 1;
    printf("[worker %d] incremented counter: %d -> %d\n",
           id, before, g_shared_counter);
    co_yield();

    /* --- leave critical section --- */
    sem_post(&g_sem);
    printf("[worker %d] done\n", id);
}

/* ---------------------------------------------------------------
 * Mutex convenience wrapper (binary semaphore)
 * --------------------------------------------------------------- */

typedef coro_sem_t coro_mutex_t;

static void mutex_init(coro_mutex_t *m, const char *name) {
    sem_init(m, 1, name);
}

static void mutex_lock(coro_mutex_t *m)   { sem_wait(m); }
static void mutex_unlock(coro_mutex_t *m) { sem_post(m); }

/* ---------------------------------------------------------------
 * Mutex demo: readers update a shared log under a mutex
 * --------------------------------------------------------------- */

#define NUM_LOGGERS 3
static coro_mutex_t g_log_mutex;
static char g_log[256];

static void logger(void *arg) {
    int id = (int)(intptr_t)arg;
    printf("[logger %d] started\n", id);
    co_yield();

    mutex_lock(&g_log_mutex);
    {
        char entry[64];
        snprintf(entry, sizeof(entry), "logger-%d;", id);
        strncat(g_log, entry, sizeof(g_log) - strlen(g_log) - 1);
        printf("[logger %d] appended to log: \"%s\"\n", id, g_log);
        co_yield();   /* yield while holding lock to show exclusion */
    }
    mutex_unlock(&g_log_mutex);
    printf("[logger %d] done\n", id);
}

/* ---------------------------------------------------------------
 * main
 * --------------------------------------------------------------- */

int main(void) {
    printf("=== Semaphore demo (counting, %d slots, %d workers) ===\n\n",
           NUM_SLOTS, NUM_WORKERS);

    sem_init(&g_sem, NUM_SLOTS, "pool");

    for (int i = 0; i < NUM_WORKERS; i++) {
        coro_spawn(worker, (void *)(intptr_t)i);
    }

    coro_run();

    printf("\nFinal counter value: %d (expected %d)\n\n",
           g_shared_counter, NUM_WORKERS);

    /* ---------------------------------------------------------- */
    printf("=== Mutex demo (%d loggers, 1 slot) ===\n\n", NUM_LOGGERS);

    mutex_init(&g_log_mutex, "log-mutex");
    memset(g_log, 0, sizeof(g_log));

    for (int i = 0; i < NUM_LOGGERS; i++) {
        coro_spawn(logger, (void *)(intptr_t)i);
    }

    coro_run();

    printf("\nFinal log: \"%s\"\n", g_log);
    return 0;
}

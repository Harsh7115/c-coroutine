/*
 * work_stealing.c — Work-stealing task pool demo using c-coroutine
 *
 * Demonstrates how a cooperative coroutine scheduler can approximate
 * work-stealing: multiple "worker" coroutines maintain local queues of
 * tasks, and when a worker runs out of work it "steals" tasks from a
 * shared global queue rather than blocking.
 *
 * Layout
 * ------
 *   - global_queue  : shared ring-buffer of pending tasks (simulated)
 *   - worker[N]     : N coroutines, each draining its own local queue
 *                     then stealing from global_queue
 *   - dispatcher    : one coroutine that pushes tasks into global_queue
 *                     and co_yields after each push so workers get CPU
 *
 * Because all coroutines run cooperatively on one OS thread there are
 * no data races, no mutexes, and no atomic operations required.
 *
 * Build:
 *   gcc -O2 -I../include -o work_stealing work_stealing.c ../src/coroutine.c
 * Run:
 *   ./work_stealing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coroutine.h"

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

#define NUM_WORKERS   4
#define TOTAL_TASKS   20
#define LOCAL_CAP     4    /* max tasks a worker keeps locally */
#define GLOBAL_CAP    32

/* ------------------------------------------------------------------ */
/* Simple task representation                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int  id;
    int  cost;   /* simulated work cost (loop iterations) */
} Task;

/* ------------------------------------------------------------------ */
/* Lock-free (single-threaded) ring-buffer queue                        */
/* ------------------------------------------------------------------ */

typedef struct {
    Task  items[GLOBAL_CAP];
    int   head;
    int   tail;
    int   size;
} TaskQueue;

static void tq_init(TaskQueue *q)  { memset(q, 0, sizeof(*q)); }
static int  tq_empty(TaskQueue *q) { return q->size == 0; }
static int  tq_full(TaskQueue *q)  { return q->size == GLOBAL_CAP; }

static int tq_push(TaskQueue *q, Task t)
{
    if (tq_full(q)) return 0;
    q->items[q->tail] = t;
    q->tail = (q->tail + 1) % GLOBAL_CAP;
    q->size++;
    return 1;
}

static int tq_pop(TaskQueue *q, Task *out)
{
    if (tq_empty(q)) return 0;
    *out = q->items[q->head];
    q->head = (q->head + 1) % GLOBAL_CAP;
    q->size--;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Globals shared between coroutines                                    */
/* ------------------------------------------------------------------ */

static TaskQueue global_queue;
static int       tasks_completed = 0;

/* ------------------------------------------------------------------ */
/* Worker coroutine                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    int        id;
    Task       local[LOCAL_CAP];
    int        local_size;
    int        stolen;
    int        processed;
} WorkerState;

static void worker_fn(void *arg)
{
    WorkerState *w = (WorkerState *)arg;

    printf("[worker %d] started
", w->id);

    for (;;) {
        /* 1. Drain local queue first */
        while (w->local_size > 0) {
            Task t = w->local[--w->local_size];

            /* Simulate task execution: busy-loop 'cost' times */
            volatile int sink = 0;
            for (int i = 0; i < t.cost * 1000; i++) sink++;
            (void)sink;

            printf("[worker %d] finished task %d (cost=%d)
",
                   w->id, t.id, t.cost);
            w->processed++;
            tasks_completed++;
            co_yield();
        }

        /* 2. Try to steal from the global queue */
        Task stolen_tasks[LOCAL_CAP];
        int  n = 0;
        while (n < LOCAL_CAP && tq_pop(&global_queue, &stolen_tasks[n]))
            n++;

        if (n == 0) {
            /* Nothing left anywhere — check if all tasks are done */
            if (tasks_completed >= TOTAL_TASKS)
                break;
            /* Otherwise yield and try again next round */
            co_yield();
            continue;
        }

        /* Copy stolen tasks into local queue */
        for (int i = 0; i < n; i++)
            w->local[w->local_size++] = stolen_tasks[i];

        w->stolen += n;
        printf("[worker %d] stole %d task(s) from global queue
", w->id, n);
        co_yield();
    }

    printf("[worker %d] done — processed=%d stolen=%d\n",
           w->id, w->processed, w->stolen);
}

/* ------------------------------------------------------------------ */
/* Dispatcher coroutine: produces all tasks into the global queue       */
/* ------------------------------------------------------------------ */

static void dispatcher_fn(void *arg)
{
    (void)arg;
    printf("[dispatcher] pushing %d tasks\n", TOTAL_TASKS);

    for (int i = 0; i < TOTAL_TASKS; i++) {
        Task t = { .id = i + 1, .cost = (i % 5) + 1 };
        while (!tq_push(&global_queue, t))
            co_yield();   /* queue full — let workers drain it */

        printf("[dispatcher] pushed task %d (cost=%d)\n", t.id, t.cost);
        co_yield();       /* give workers a chance to steal */
    }

    printf("[dispatcher] all tasks enqueued\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    tq_init(&global_queue);

    /* Initialise coroutine library */
    co_init();

    /* Create dispatcher */
    co_create(dispatcher_fn, NULL);

    /* Create workers */
    WorkerState workers[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++) {
        memset(&workers[i], 0, sizeof(workers[i]));
        workers[i].id = i;
        co_create(worker_fn, &workers[i]);
    }

    /* Run until all coroutines finish */
    co_run();

    /* Summary */
    printf("\n=== Summary ===\n");
    printf("Total tasks completed: %d\n", tasks_completed);
    for (int i = 0; i < NUM_WORKERS; i++) {
        printf("  worker %d: processed=%d stolen=%d\n",
               i, workers[i].processed, workers[i].stolen);
    }

    co_cleanup();
    return 0;
}

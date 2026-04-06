/*
 * fiber_pool.c — Fixed-size fiber pool built on top of c-coroutine
 *
 * Demonstrates a reusable pool of N worker fibers that each process
 * tasks from a shared queue.  The main coroutine acts as the producer,
 * enqueuing work items, while pool fibers consume them and co_yield
 * back after each unit of work.
 *
 * Build:
 *   gcc -O2 -o fiber_pool fiber_pool.c -L.. -lcoroutine -I../include
 *
 * Usage:
 *   ./fiber_pool [num_fibers] [num_tasks]
 *   Default: 4 fibers, 20 tasks
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coroutine.h"

/* ------------------------------------------------------------------ */
/*  Simple bounded task queue                                           */
/* ------------------------------------------------------------------ */

#define QUEUE_CAP 64

typedef struct {
    int items[QUEUE_CAP];
    int head, tail, size;
} TaskQueue;

static void queue_init(TaskQueue *q) {
    q->head = q->tail = q->size = 0;
}

static int queue_push(TaskQueue *q, int val) {
    if (q->size == QUEUE_CAP) return -1;
    q->items[q->tail] = val;
    q->tail = (q->tail + 1) % QUEUE_CAP;
    q->size++;
    return 0;
}

static int queue_pop(TaskQueue *q, int *out) {
    if (q->size == 0) return -1;
    *out = q->items[q->head];
    q->head = (q->head + 1) % QUEUE_CAP;
    q->size--;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Fiber pool state                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int        id;          /* worker id (0-based)                     */
    TaskQueue *queue;       /* shared task queue                       */
    int        processed;   /* count of tasks handled by this worker   */
} WorkerArg;

/*
 * worker_fn — entry point for each pool fiber.
 *
 * Loops: pop a task → simulate work → co_yield → repeat.
 * Exits when the queue is empty and the producer signals done.
 */
static void worker_fn(void *arg) {
    WorkerArg *w = (WorkerArg *)arg;
    int task;

    while (1) {
        if (queue_pop(w->queue, &task) == 0) {
            /* Simulate processing: just print and count */
            printf("  [worker %d] processing task %d\n", w->id, task);
            w->processed++;
        }
        /* Yield back to scheduler regardless — lets other fibers run */
        co_yield();
    }
}

/* ------------------------------------------------------------------ */
/*  Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    int num_fibers = (argc > 1) ? atoi(argv[1]) : 4;
    int num_tasks  = (argc > 2) ? atoi(argv[2]) : 20;

    if (num_fibers < 1 || num_fibers > 32) {
        fprintf(stderr, "num_fibers must be 1–32\n");
        return 1;
    }
    if (num_tasks < 1 || num_tasks > QUEUE_CAP) {
        fprintf(stderr, "num_tasks must be 1–%d\n", QUEUE_CAP);
        return 1;
    }

    printf("fiber_pool: %d fibers, %d tasks\n", num_fibers, num_tasks);

    /* Shared queue */
    TaskQueue queue;
    queue_init(&queue);

    /* Enqueue all tasks up front */
    for (int i = 0; i < num_tasks; i++) {
        queue_push(&queue, i + 1);
    }

    /* Allocate worker args */
    WorkerArg *args = calloc(num_fibers, sizeof(WorkerArg));
    if (!args) { perror("calloc"); return 1; }

    /* Spawn fibers */
    coroutine_t **fibers = calloc(num_fibers, sizeof(coroutine_t *));
    if (!fibers) { perror("calloc"); return 1; }

    for (int i = 0; i < num_fibers; i++) {
        args[i].id        = i;
        args[i].queue     = &queue;
        args[i].processed = 0;
        fibers[i] = coroutine_create(worker_fn, &args[i]);
        if (!fibers[i]) {
            fprintf(stderr, "failed to create fiber %d\n", i);
            return 1;
        }
    }

    /*
     * Round-robin scheduling: keep resuming fibers until the queue is
     * drained and all workers are idle for a full cycle.
     */
    int idle_rounds = 0;
    while (idle_rounds < num_fibers) {
        idle_rounds = 0;
        for (int i = 0; i < num_fibers; i++) {
            int before = args[i].processed;
            coroutine_resume(fibers[i]);
            if (args[i].processed == before && queue.size == 0) {
                idle_rounds++;
            }
        }
    }

    /* Print summary */
    printf("\nResults:\n");
    int total = 0;
    for (int i = 0; i < num_fibers; i++) {
        printf("  worker %d: %d tasks\n", i, args[i].processed);
        total += args[i].processed;
    }
    printf("Total processed: %d / %d\n", total, num_tasks);

    /* Cleanup */
    for (int i = 0; i < num_fibers; i++) {
        coroutine_destroy(fibers[i]);
    }
    free(fibers);
    free(args);

    return (total == num_tasks) ? 0 : 1;
}

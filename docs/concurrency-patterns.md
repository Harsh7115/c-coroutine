# Concurrency Patterns

Cooperative coroutines are single-threaded, so traditional race conditions are impossible — a coroutine runs uninterrupted until it calls `co_yield()` or `co_await()`. This page catalogs common concurrency patterns and shows how to implement them cleanly on top of the `c-coroutine` API.

---

## 1. Mutex (Critical Section)

In a cooperative system a mutex is just a flag: if the flag is held, yield until it is released.

```c
#include "coroutine.h"
#include <stdbool.h>

typedef struct {
    bool locked;
} CoMutex;

void co_mutex_init(CoMutex *m)  { m->locked = false; }

void co_mutex_lock(CoMutex *m) {
    while (m->locked)
        co_yield();   /* give the holder a chance to unlock */
    m->locked = true;
}

void co_mutex_unlock(CoMutex *m) { m->locked = false; }
```

**Usage**

```c
static CoMutex g_lock;
static int     g_counter = 0;

static void incrementer(void *arg) {
    int n = *(int *)arg;
    for (int i = 0; i < n; i++) {
        co_mutex_lock(&g_lock);
        g_counter++;
        co_mutex_unlock(&g_lock);
        co_yield();
    }
}

int main(void) {
    co_mutex_init(&g_lock);
    int n = 100;
    Coroutine *a = co_create(incrementer, &n, 0);
    Coroutine *b = co_create(incrementer, &n, 0);
    co_run();
    /* g_counter is deterministically 200 */
    co_free(a); co_free(b);
}
```

---

## 2. Semaphore (Counting Gate)

A semaphore limits the number of coroutines that can be in a section simultaneously.

```c
typedef struct {
    int count;
} CoSemaphore;

void co_sem_init(CoSemaphore *s, int n)  { s->count = n; }

void co_sem_wait(CoSemaphore *s) {
    while (s->count <= 0)
        co_yield();
    s->count--;
}

void co_sem_post(CoSemaphore *s) { s->count++; }
```

**Pattern — connection pool with max 3 concurrent workers**

```c
static CoSemaphore pool_sem;

static void worker(void *arg) {
    co_sem_wait(&pool_sem);      /* acquire a slot */
    /* ... do work ... */
    co_sem_post(&pool_sem);      /* release slot */
}

int main(void) {
    co_sem_init(&pool_sem, 3);   /* pool size = 3 */
    /* spawn N > 3 workers — at most 3 run past the gate at once */
}
```

---

## 3. Barrier (Rendezvous)

All coroutines block at the barrier until every participant has arrived, then all are released together.

```c
typedef struct {
    int total;     /* number of participants */
    int waiting;   /* how many have arrived */
    int phase;     /* incremented each time the barrier releases */
} CoBarrier;

void co_barrier_init(CoBarrier *b, int n) {
    b->total = n; b->waiting = 0; b->phase = 0;
}

void co_barrier_wait(CoBarrier *b) {
    int my_phase = b->phase;
    b->waiting++;
    if (b->waiting == b->total) {
        b->waiting = 0;
        b->phase++;          /* release everyone */
    } else {
        while (b->phase == my_phase)
            co_yield();      /* spin-yield until phase advances */
    }
}
```

**Pattern — parallel map with synchronisation**

```c
#define N_WORKERS 4
static CoBarrier barrier;
static int data[N_WORKERS];

static void map_worker(void *arg) {
    int id = *(int *)arg;
    data[id] = id * id;         /* phase 1: compute */
    co_barrier_wait(&barrier);
    /* all workers have finished phase 1 — safe to read neighbours */
    int left  = (id > 0)          ? data[id-1] : 0;
    int right = (id < N_WORKERS-1)? data[id+1] : 0;
    data[id] = left + data[id] + right;
}

int main(void) {
    co_barrier_init(&barrier, N_WORKERS);
    /* ... create and run N_WORKERS coroutines ... */
}
```

---

## 4. Bounded Queue (Producer / Consumer)

A fixed-capacity ring buffer shared between one producer and one consumer coroutine. The producer yields when the queue is full; the consumer yields when it is empty.

```c
#define QCAP 8

typedef struct {
    int  buf[QCAP];
    int  head, tail, size;
} CoQueue;

void cq_init(CoQueue *q)          { q->head = q->tail = q->size = 0; }
bool cq_full(CoQueue *q)          { return q->size == QCAP; }
bool cq_empty(CoQueue *q)         { return q->size == 0; }

void cq_push(CoQueue *q, int val) {
    while (cq_full(q))  co_yield();
    q->buf[q->tail] = val;
    q->tail = (q->tail + 1) % QCAP;
    q->size++;
}

int cq_pop(CoQueue *q) {
    while (cq_empty(q)) co_yield();
    int val = q->buf[q->head];
    q->head = (q->head + 1) % QCAP;
    q->size--;
    return val;
}
```

See `examples/producer_consumer.c` for a complete runnable demo.

---

## 5. Future / Promise (Single-Value Channel)

A lightweight future allows one coroutine to await a value produced by another.

```c
typedef struct {
    bool ready;
    int  value;
} CoFuture;

void co_future_init(CoFuture *f)         { f->ready = false; }
void co_future_set(CoFuture *f, int v)   { f->value = v; f->ready = true; }

int co_future_get(CoFuture *f) {
    while (!f->ready) co_yield();
    return f->value;
}
```

**Usage**

```c
static CoFuture result;

static void producer(void *arg) {
    /* ... compute ... */
    co_future_set(&result, 42);
}

static void consumer(void *arg) {
    int v = co_future_get(&result);   /* blocks until producer sets it */
    printf("got %d\n", v);
}
```

---

## 6. Fan-Out / Fan-In

Distribute work across N workers and collect results back through a shared queue.

```c
/* Fan-out: main coroutine pushes N tasks into a shared work queue */
/* Fan-in:  each worker pushes its result into a results queue     */

static CoQueue work_q, result_q;

static void worker(void *arg) {
    while (true) {
        int task = cq_pop(&work_q);
        if (task < 0) break;        /* sentinel: no more work */
        cq_push(&result_q, task * task);
    }
}

int main(void) {
    cq_init(&work_q);
    cq_init(&result_q);

    int n = 4;
    for (int i = 0; i < n; i++)
        co_create(worker, NULL, 0);

    /* Push 16 tasks */
    for (int i = 0; i < 16; i++) cq_push(&work_q, i);
    /* Push N sentinels (one per worker) */
    for (int i = 0; i < n; i++)  cq_push(&work_q, -1);

    co_run();

    /* Drain results */
    while (!cq_empty(&result_q))
        printf("%d\n", cq_pop(&result_q));
}
```

---

## Choosing the Right Pattern

| Need | Pattern |
|------|---------|
| Exclusive access to a resource | Mutex |
| Limit concurrency to K | Semaphore |
| Synchronise N at a checkpoint | Barrier |
| Stream of items between two coroutines | Bounded Queue |
| Single result from async task | Future |
| Parallel work with aggregation | Fan-Out / Fan-In |

---

## Notes on Fairness

All patterns above use **spin-yield** loops (`while (condition) co_yield()`). In a FIFO scheduler (the default) this is fair: each waiting coroutine gets exactly one turn per scheduler round, and starvation is impossible as long as the condition eventually becomes false.

If you have many coroutines waiting on the same condition and want to avoid O(n) wake-up scans, consider using an explicit wait-list (a linked list of waiting coroutine IDs) and only re-enqueuing them when the condition changes — similar to the `co_await` wakeup mechanism in the library core.

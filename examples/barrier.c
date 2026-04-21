/*
 * examples/barrier.c
 *
 * A reusable cooperative barrier built on top of the c-coroutine library.
 *
 * A barrier is a synchronization primitive that blocks a set of participants
 * until all of them have arrived. Once the last coroutine reaches the
 * barrier, every waiting coroutine is released and the barrier is reset so
 * it can be used again for the next phase.
 *
 * Because the scheduler is cooperative and single-threaded, this barrier
 * implementation does not need any locks or atomics. Each coroutine that
 * calls cob_wait() simply increments the arrival counter and co_yields
 * until the required count is reached.
 *
 * Build:
 *     gcc -Iinclude examples/barrier.c lib/libcoroutine.a -o barrier
 *     ./barrier
 */

#include "include/coroutine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int required;   /* total participants expected */
    int arrived;    /* arrived so far this phase   */
    int phase;      /* increments on release       */
} CoBarrier;

static void cob_init(CoBarrier *b, int n_participants) {
    memset(b, 0, sizeof(*b));
    b->required = n_participants;
}

/*
 * Block the calling coroutine until 'required' coroutines have also called
 * cob_wait. The last arrival prints a release banner, advances the phase
 * counter, and resets the arrival count for the next round.
 */
static void cob_wait(CoBarrier *b) {
    b->arrived++;

    if (b->arrived >= b->required) {
        printf("*** all coroutines reached barrier -- releasing ***\n");
        b->arrived = 0;
        b->phase++;
        return;
    }

    int my_phase = b->phase;
    while (b->phase == my_phase) {
        co_yield();
    }
}

/* -------------------------- demo workload -------------------------- */

static CoBarrier gate;

static void worker(void *arg) {
    int id = (int)(long)arg;

    printf("[%d] phase 1 done, waiting at barrier\n", id);
    cob_wait(&gate);
    printf("[%d] phase 2 start\n", id);
}

int main(void) {
    enum { N = 4 };
    cob_init(&gate, N);

    Coroutine *cs[N];
    for (int i = 0; i < N; i++) {
        cs[i] = co_create(worker, (void *)(long)(i + 1), 0);
    }

    co_run();

    for (int i = 0; i < N; i++) co_free(cs[i]);
    return 0;
}

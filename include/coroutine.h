/*
 * coroutine.h — cooperative userspace coroutine (fiber) library
 *
 * Coroutines are cooperatively scheduled: a running coroutine holds the CPU
 * until it calls co_yield() or co_await().  The scheduler then picks the next
 * READY coroutine and resumes it.
 *
 * Usage:
 *
 *   void my_task(void *arg) {
 *       printf("hello from coroutine %d\n", co_id());
 *       co_yield();
 *       printf("resumed\n");
 *   }
 *
 *   int main(void) {
 *       Coroutine *c = co_create(my_task, NULL, 0);
 *       co_run();          // blocks until all coroutines finish
 *       co_free(c);
 *   }
 */

#pragma once
#include <stddef.h>

/* Opaque coroutine handle. */
typedef struct Coroutine Coroutine;

/* Coroutine lifecycle states. */
typedef enum {
    CO_READY   = 0,  /* created, or resumed after a yield — eligible to run */
    CO_RUNNING = 1,  /* currently executing                                  */
    CO_WAITING = 2,  /* suspended in co_await(), blocked on another          */
    CO_DONE    = 3,  /* function returned; resources can be freed             */
} CoState;

/*
 * co_create — allocate and initialise a new coroutine.
 *
 *   fn         : coroutine body; must not return a value (use co_yield to pause)
 *   arg        : opaque argument forwarded to fn
 *   stack_size : byte size of the private stack; 0 → library default (256 KB)
 *
 * Returns NULL on allocation failure.
 * The new coroutine is placed in CO_READY state and will be picked up by
 * the next call to co_run() or by the currently running scheduler pass.
 */
Coroutine *co_create(void (*fn)(void *), void *arg, size_t stack_size);

/*
 * co_free — release all resources owned by a coroutine.
 *
 * Must only be called after the coroutine reaches CO_DONE.
 * The handle is invalid after this call.
 */
void co_free(Coroutine *co);

/*
 * co_yield — voluntarily surrender the CPU.
 *
 * The calling coroutine transitions from CO_RUNNING → CO_READY and is
 * re-enqueued.  The scheduler selects the next READY coroutine to run.
 * Must be called from within a coroutine (not from main / co_run).
 */
void co_yield(void);

/*
 * co_await — suspend until another coroutine completes.
 *
 * The calling coroutine transitions to CO_WAITING.  It will be moved back to
 * CO_READY automatically when `other` reaches CO_DONE.
 * Calling co_await on a CO_DONE coroutine returns immediately.
 * Must be called from within a coroutine.
 */
void co_await(Coroutine *other);

/*
 * co_run — start the scheduler loop.
 *
 * Runs until every registered coroutine is in CO_DONE.
 * Typically called once from main() after creating all initial coroutines.
 */
void co_run(void);

/*
 * co_id — return the integer ID of the currently executing coroutine.
 *
 * IDs are assigned sequentially starting from 1.
 * Returns 0 when called from outside a coroutine (e.g., from main).
 */
int co_id(void);

/*
 * co_state — return the current lifecycle state of a coroutine.
 */
CoState co_state(const Coroutine *co);

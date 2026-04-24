# Scheduler Internals

This document explains how the cooperative scheduler inside `c-coroutine` works — from the
run-queue data structure to the context-switch assembly stub.

---

## Overview

`c-coroutine` uses a **FIFO cooperative scheduler**: coroutines run until they explicitly
yield control (via `co_yield`) or return.  The scheduler never interrupts a running
coroutine; preemption is left to the caller's design.

```
                 +------------------------------+
  co_spawn() -->|         Run Queue (FIFO)      |
                |  [coro_A] -> [coro_B] -> ...  |
                +----------+-------------------+
                           | scheduler_next()
                           v
                    Context Switch
                  (save SP/IP/callee-saves)
                           |
                           v
                  Coroutine runs until
                  co_yield() or return
```

---

## Data Structures

### coroutine_t

```c
typedef struct coroutine {
    uintptr_t  rsp;          /* saved stack pointer                  */
    uintptr_t  rip;          /* saved instruction pointer            */
    uint8_t   *stack_base;   /* bottom of the allocated stack        */
    size_t     stack_size;   /* total stack size in bytes            */
    co_state_t state;        /* READY | RUNNING | SUSPENDED | DEAD   */
    co_fn_t    fn;           /* entry-point function                 */
    void      *arg;          /* argument passed to fn                */
    struct coroutine *next;  /* intrusive linked-list link           */
} coroutine_t;
```

Fields are kept minimal to avoid heap fragmentation. The intrusive `next` pointer
doubles as the free-list link when a coroutine slot is recycled.

### Run Queue

```c
typedef struct {
    coroutine_t *head;   /* dequeue from here  */
    coroutine_t *tail;   /* enqueue here       */
    size_t       count;
} run_queue_t;
```

Both enqueue (O(1), append to tail) and dequeue (O(1), pop from head) are constant
time. The queue is singly-linked; FIFO ordering is strictly maintained.

---

## Context Switch (x86-64)

The low-level switch lives in `src/ctx_switch.S`. It follows the System V AMD64 ABI:
caller-saved registers (rax, rcx, rdx, rsi, rdi, r8-r11) are NOT saved because the
coroutine never expects them to survive a yield. Only callee-saved registers
(rbx, rbp, r12-r15) and the stack pointer are preserved.

```asm
# void _ctx_switch(coroutine_t *from, coroutine_t *to)
#   rdi = from,  rsi = to
_ctx_switch:
    # --- save current context ---
    push   %rbp
    push   %rbx
    push   %r12
    push   %r13
    push   %r14
    push   %r15
    movq   %rsp, 0(%rdi)    # from->rsp = rsp

    # --- load next context ---
    movq   0(%rsi), %rsp    # rsp = to->rsp
    pop    %r15
    pop    %r14
    pop    %r13
    pop    %r12
    pop    %rbx
    pop    %rbp
    ret                     # jumps to to->rip
```

The ret instruction pops the saved return address from the new stack, seamlessly
resuming the next coroutine where it last yielded.

---

## Scheduler Loop

```c
void scheduler_run(void) {
    while (run_queue.count > 0) {
        coroutine_t *next = rq_dequeue(&run_queue);
        if (next->state == CO_DEAD) {
            stack_free(next);
            continue;
        }
        next->state = CO_RUNNING;
        current = next;
        _ctx_switch(&scheduler_ctx, next);
        /* resumes here after co_yield() or co_return */
    }
}
```

scheduler_ctx is a special pseudo-coroutine that holds the scheduler's own stack
frame. Every co_yield() switches back to scheduler_ctx, which then picks the
next runnable coroutine.

---

## co_yield Implementation

```c
void co_yield(void) {
    coroutine_t *self = current;
    self->state = CO_SUSPENDED;
    rq_enqueue(&run_queue, self);       /* re-queue at the tail */
    _ctx_switch(self, &scheduler_ctx); /* switch back to scheduler */
}
```

Suspend self, re-enqueue, switch. Because the queue is FIFO, all other runnable
coroutines get a turn before self runs again — round-robin without a timer.

---

## Stack Allocation

Each coroutine gets a private stack via mmap(MAP_ANONYMOUS | MAP_PRIVATE). A guard
page (PROT_NONE) is placed below the stack to catch overflows at the OS level.

```c
uint8_t *stack_alloc(size_t size) {
    /* one extra page for the guard */
    uint8_t *mem = mmap(NULL, size + PAGE_SIZE,
                        PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mem == MAP_FAILED) return NULL;
    mprotect(mem, PAGE_SIZE, PROT_NONE);  /* guard page */
    return mem + PAGE_SIZE;               /* usable region starts above guard */
}
```

Stacks grow downward on x86-64, so the guard page sits at the lowest address.

---

## Adding a New Scheduler Policy

The scheduler is easy to extend. To swap in a priority scheduler:

1. Replace run_queue_t with a min-heap keyed on priority.
2. Change rq_enqueue / rq_dequeue accordingly.
3. Add a priority field to coroutine_t and expose it via co_spawn_prio(fn, arg, prio).

The context-switch stub (_ctx_switch) is policy-agnostic and requires no changes.

---

## See Also

- src/ctx_switch.S  -- assembly context-switch stub
- src/scheduler.c   -- run-queue operations and scheduler_run()
- include/coroutine.h -- public API
- docs/memory-model.md -- co_yield memory ordering guarantees

# c-coroutine — Design Notes

This document explains the internals of the library for contributors and curious readers. It covers the two context-switch backends, the scheduler data structures, and the lifetime of a coroutine.

## Overview

A coroutine (`Coroutine`) is a heap-allocated object with its own stack and saved register state. Coroutines are cooperatively scheduled: the currently-running coroutine must explicitly yield control via `co_yield()` or block via `co_await()`.

```
┌─────────────┐  co_yield()   ┌─────────────┐
│  Coroutine A│ ────────────► │  Scheduler  │
└─────────────┘               │  (run queue)│
                               └──────┬──────┘
                                      │ co_ctx_switch()
                               ┌──────▼──────┐
                               │  Coroutine B│
                               └─────────────┘
```

## Coroutine Object

```c
typedef struct Coroutine {
    CoContext    ctx;          // saved register file
    CoState      state;        // READY / RUNNING / WAITING / DONE
    char        *stack;        // heap-allocated stack (default 64 KiB)
    size_t       stack_size;
    co_fn        fn;           // user entry function
    void        *arg;
    uint64_t     id;
    struct Coroutine *waiting_on;  // co_await target
    /* intrusive linked-list pointers for wait-list & run-queue */
    struct Coroutine *next;
} Coroutine;
```

## Context Switch (x86-64 ASM backend)

The heart of the library is `co_ctx_switch(CoContext *from, CoContext *to)` in `src/asm_ctx.S`. It saves the **seven callee-saved registers** required by the System V AMD64 ABI plus the stack pointer, then restores the same from the destination context:

```
Saved registers (in order on the stack / in CoContext):
  %rbx  %rbp  %r12  %r13  %r14  %r15  %rsp
```

Caller-saved registers (`%rax`, `%rcx`, `%rdx`, `%rsi`, `%rdi`, `%r8–%r11`) are **not** saved — the calling convention already guarantees callee can trash them.

The first time a coroutine is switched to, execution lands in `co_trampoline`, which pops the `Coroutine*` pointer into `%rdi` (the first SysV argument register) and jumps to `co_entry`.

### Stack layout at first switch

```
high address ───────────────────────────────
             [ ... guard page (optional) ... ]
             [ co_trampoline address         ]  ← initial rsp points here
             [ Coroutine* pointer            ]
low address  ────────────────────────────────
```

## ucontext Fallback

On non-x86-64 targets (or when `USE_UCONTEXT=1`), the library uses POSIX `makecontext` / `swapcontext`. Performance is roughly 3–5× slower due to signal-mask manipulation inside `swapcontext`, but semantics are identical.

## Scheduler

The scheduler is a global FIFO run-queue implemented as a singly-linked list of `Coroutine*` pointers. The main thread gets an implicit "main coroutine" context so that `co_ctx_switch` can safely return to the top-level `co_run()` call when the queue empties.

### Scheduling loop (`co_run`)

```
while run_queue is not empty:
    next = dequeue()
    next->state = RUNNING
    co_ctx_switch(&scheduler_ctx, &next->ctx)
    // returned here after next yields or finishes
    if next->state == DONE:
        wakeup_waiters(next)
        free_coroutine(next)
```

### `co_yield`

Sets `state = READY`, re-enqueues self, then switches back to the scheduler context.

### `co_await(target)`

Sets `state = WAITING`, appends self to `target->wait_list`, then switches to the scheduler without re-enqueuing. When `target` finishes, `wakeup_waiters` sets each waiter back to `READY` and re-enqueues them.

## Memory Management

Each coroutine stack is `malloc`'d at `co_create` time and `free`'d after the coroutine reaches `DONE` (either by the scheduler or by an explicit `co_free`). The `Coroutine` struct itself is also heap-allocated.

There is no guard page by default; adding `mprotect` guard pages is on the roadmap.

## Known Limitations

- **Single-threaded only**: the scheduler is not thread-safe. Running coroutines from multiple OS threads requires external locking and is unsupported.
- **No preemption**: a coroutine that never calls `co_yield` will starve others.
- **Stack overflow is silent**: without guard pages, a stack overflow produces undefined behaviour.
- **Signal handling**: signals are delivered to the currently-running coroutine's stack; behaviour with `sigaltstack` is untested.

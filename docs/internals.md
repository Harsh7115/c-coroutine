# Coroutine Library Internals

This document describes the internal architecture of the c-coroutine library —
how context switching works, how the scheduler manages coroutines, and the
memory layout of each fiber.

---

## 1. Overview

At its core, the library models each coroutine as an independent **execution
context**: a saved register file, a dedicated stack, and a small metadata
block.  Switching between contexts is done without OS involvement; every yield
point is a plain function call that saves and restores CPU registers by hand.

---

## 2. Context Layout (`coroutine_t`)

```
struct coroutine {
    uint8_t        *stack_base;   /* bottom of allocated stack (for free()) */
    size_t          stack_size;   /* size in bytes                          */
    context_t       ctx;          /* saved CPU registers (see below)        */
    coro_state_t    state;        /* READY | RUNNING | SUSPENDED | DEAD      */
    coro_fn_t       fn;           /* entry-point function pointer           */
    void           *arg;          /* argument passed to fn()                */
    struct coroutine *caller;     /* context that resumed us                */
};
```

### 2.1 `context_t` — The Register File

On x86-64 System V ABI, only the *callee-saved* registers need to be preserved
across a context switch.  The library saves:

| Register | Purpose |
|----------|---------|
| `%rsp`  | stack pointer — core of the switch |
| `%rbp`  | frame pointer |
| `%rbx`  | general callee-save |
| `%r12` – `%r15` | general callee-saves |
| `%rip`  | instruction pointer (stored implicitly via `call`/`ret`) |

The SIMD registers (`%xmm6`–`%xmm15` on Windows) are **not** saved because
the library targets Linux/macOS only.  Floating-point state is the caller's
responsibility.

---

## 3. Context Switch (`context_switch.S`)

The switch is implemented in two tiny x86-64 assembly routines:

```asm
/* Save caller, restore callee */
co_switch(context_t *save_to, const context_t *load_from):
    /* push all callee-saved regs onto the current stack */
    push   %rbp
    push   %rbx
    push   %r12
    push   %r13
    push   %r14
    push   %r15
    mov    %rsp, (%rdi)     /* save current SP into save_to->rsp */

    mov    (%rsi), %rsp     /* load new SP from load_from->rsp   */
    pop    %r15
    pop    %r14
    pop    %r13
    pop    %r12
    pop    %rbx
    pop    %rbp
    ret                     /* pops saved RIP → resumes callee   */
```

The first call to a freshly created coroutine works because `co_create()`
manually plants the entry-point address at the top of the new stack, so the
`ret` instruction jumps there the first time.

---

## 4. Stack Allocation

Each coroutine receives a private stack allocated with `mmap(MAP_ANONYMOUS)`.
A **guard page** (one page, `PROT_NONE`) is placed immediately below the
stack so that overflow raises a segfault rather than silently corrupting the
heap.

```
  high address
  +-----------------+   <- stack_base + stack_size
  |   usable stack  |
  |       ...       |
  +-----------------+   <- stack_base + PAGE_SIZE
  |   guard page    |   PROT_NONE
  +-----------------+   <- stack_base
  low address
```

Typical stack size is **64 KiB**; deep-recursive coroutines should request
more via `co_create_ex(fn, arg, stack_bytes)`.

---

## 5. Scheduler

The default scheduler is a simple **FIFO run queue** implemented as a circular
doubly-linked list.

```
co_schedule(coro)   →  enqueue at tail
co_yield()          →  enqueue self at tail, dequeue head, co_switch to it
co_run()            →  loop: dequeue head, co_switch to it, until queue empty
```

Because the scheduler is cooperative, a coroutine that never calls `co_yield()`
will starve all others.  This is by design: the library targets deterministic,
low-latency use cases where the programmer controls yield points.

### 5.1 Ready Queue vs. Waiting Set

| Collection | Contents |
|-----------|---------|
| Run queue  | Coroutines eligible to run immediately |
| *(future)* wait set | Coroutines blocked on I/O or a channel |

The current release only has a run queue.  A future release will add
`co_await_fd()` backed by `epoll`.

---

## 6. co_yield vs co_await

| Primitive | Behaviour |
|-----------|-----------|
| `co_yield()` | Suspends self, re-enqueues self, runs next ready coro |
| `co_await(coro)` | Suspends self, switches directly to *coro*; *coro* resumes caller on its next `co_yield()` |

`co_await` bypasses the scheduler for tight producer/consumer pairs where you
want zero round-trip latency.

---

## 7. Thread Safety

The library is **single-threaded by design**.  The run queue is not protected
by any lock.  To use coroutines across threads, create a separate scheduler
instance per thread (planned for v0.3).

---

## 8. Known Limitations

- No Windows support (uses `mmap` and x86-64 System V ABI).
- No `setjmp`/`longjmp` interoperability.
- Stack size must be chosen at creation time; there is no stack growth.
- Signal handlers run on the stack of whichever coroutine was active when the
  signal arrived — install them before starting the scheduler.

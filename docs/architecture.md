# Coroutine Internals

This document explains how the c-coroutine library implements cooperative multitasking on x86-64 without relying on `ucontext` or `setjmp`/`longjmp`.

## High-level model

A coroutine in this library is a heap-allocated structure that owns:

- A private stack (default 64 KiB, configurable via `co_attr_t`).
- A saved register frame: `rbx`, `rbp`, `r12`-`r15`, `rsp`, and a return `rip`.
- A scheduler-managed state: `READY`, `RUNNING`, `SUSPENDED`, or `DEAD`.

The library never uses preemption - every transition between coroutines is driven by an explicit `co_yield` or by completion of the entry function.

## Context switch

The context switch is a single hand-written assembly routine, `co_switch(from, to)`. It saves the six callee-saved registers plus `rsp` into the `from` coroutine, then loads the same fields from `to` and `ret`s into the new `rip`. Because the System V AMD64 ABI guarantees that callee-saved registers and the stack pointer are sufficient to describe a thread of execution, no FPU or SSE state needs to be saved at the boundary.

The full sequence is approximately:

    mov   [rdi+0x00], rbx
    mov   [rdi+0x08], rbp
    mov   [rdi+0x10], r12
    mov   [rdi+0x18], r13
    mov   [rdi+0x20], r14
    mov   [rdi+0x28], r15
    mov   [rdi+0x30], rsp
    mov   rbx, [rsi+0x00]
    mov   rbp, [rsi+0x08]
    mov   r12, [rsi+0x10]
    mov   r13, [rsi+0x18]
    mov   r14, [rsi+0x20]
    mov   r15, [rsi+0x28]
    mov   rsp, [rsi+0x30]
    ret

A fresh coroutine is bootstrapped by writing the address of an internal trampoline as the return address on the new stack; the first `ret` jumps into the trampoline, which in turn calls the user-supplied entry function.

## Scheduler

The default scheduler is a simple FIFO ready-queue. `co_yield` moves the current coroutine to the tail of the queue and dispatches the head. `co_await(handle)` suspends the caller until `handle` transitions to `DEAD`. The queue is single-threaded by design - there is one scheduler per thread, and coroutines do not migrate.

## Stack lifetime

Stacks are allocated with `mmap` (`MAP_PRIVATE | MAP_ANONYMOUS`) and surrounded by a single guard page on the low end so that a stack overflow produces `SIGSEGV` instead of silent corruption. They are released in `co_destroy`.

## Limitations

- x86-64 only. ARM64 support is planned but requires a different register save set.
- Not safe across `fork`. After a fork, only the calling coroutine is well-defined.
- No automatic stack growth - pick a size that fits your deepest call chain.

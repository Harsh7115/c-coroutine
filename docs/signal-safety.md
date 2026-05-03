# Signal Safety in c-coroutine

This document describes how the c-coroutine library interacts with POSIX signals,
and what guarantees the library provides (and does not provide) when signals
are raised mid-execution.

## TL;DR

- The cooperative scheduler is **not** signal-safe by default.
- `co_yield`, `co_await`, and the FIFO scheduler dispatch loop must not be
  invoked from a signal handler.
- Asynchronous-signal-safe wrappers are provided for a small subset of
  primitives — see `Async-Signal-Safe API` below.

## Why cooperative coroutines and signals are tricky

A signal handler can fire on any instruction boundary of the running thread.
If the handler tries to perform a context switch (for example, by calling
`co_yield` to suspend the running fiber), three things can go wrong:

1. **Reentrancy in the scheduler.** The FIFO ready queue uses a non-atomic
   doubly linked list. A signal that interrupts mid-enqueue and then attempts
   to enqueue itself will corrupt the list.
2. **Stack pointer aliasing.** The handler runs on the same stack as the
   currently executing coroutine. Calling `swap_context` from the handler
   would rewind `%rsp` to the previous fiber's saved frame, while the
   handler's local variables still live on the original stack. The result is
   undefined behavior on return.
3. **TLS / errno corruption.** Coroutines do not save `errno` or thread-local
   storage on context switch. A handler that reads `errno` between a syscall
   and the next instruction will observe inconsistent state.

## What is safe

The following operations are async-signal-safe and may be called from a
handler:

- `co_self()` — reads a thread-local pointer, no allocation.
- `co_id(co)` — reads a 64-bit field.
- `co_pending_signals()` — atomically loads a bitmask.
- Setting a flag that is later observed by a regular coroutine.

## What is not safe

Do **not** call any of the following from a handler:

- `co_create`, `co_destroy`, `co_join`
- `co_yield`, `co_await`, `co_resume`
- Anything that allocates (the allocator is not reentrant)
- Any I/O call that the scheduler proxies (read/write/poll wrappers)

## Recommended pattern: signalfd or self-pipe

Rather than do real work inside a handler, write a single byte to a
self-pipe (or use `signalfd` on Linux) and let a dedicated coroutine drain
the pipe in the regular scheduling loop:

```c
static int sig_pipe[2];

static void handler(int sig) {
    unsigned char b = (unsigned char)sig;
    /* write(2) is async-signal-safe */
    (void)write(sig_pipe[1], &b, 1);
}

void signal_dispatcher(void *arg) {
    (void)arg;
    unsigned char buf[16];
    for (;;) {
        ssize_t n = co_read(sig_pipe[0], buf, sizeof buf);
        for (ssize_t i = 0; i < n; i++) {
            dispatch_to_handler_coro(buf[i]);
        }
    }
}
```

This pushes all real work onto the cooperative scheduler, where the usual
invariants hold.

## Blocking signals around critical sections

If a coroutine must touch shared scheduler state (for example, when
implementing a custom `co_select`-like primitive), block signals with
`pthread_sigmask` for the duration of the critical section:

```c
sigset_t old, new;
sigfillset(&new);
pthread_sigmask(SIG_BLOCK, &new, &old);
/* critical section */
pthread_sigmask(SIG_SETMASK, &old, NULL);
```

## Per-fiber alternate stacks

A future revision of the library will support per-fiber `sigaltstack`
configuration so that signal handlers run on a dedicated stack and do not
interfere with cooperative context switches. Track progress in
`docs/roadmap.md`.

## See also

- `docs/scheduler-internals.md` — for the FIFO ready queue invariants
- `docs/memory-model-amd64.md` — for the saved register set and the
  guarantees we make about `%rsp`/`%rbp` after a yield

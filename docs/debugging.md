# Debugging Guide

Debugging cooperative coroutines is subtly different from debugging ordinary
threaded code. Because switching happens only at explicit `co_yield` or
`co_await` points, most classic race conditions disappear — but a new class
of problems takes their place: stuck schedulers, unbalanced switches, and
stacks that look "impossible" to a debugger because they live on the heap.

This document collects the techniques that have worked well when chasing
bugs in `c-coroutine`.

## Table of contents

1. Recommended build flags
2. Reading coroutine stacks in GDB
3. Detecting stack overflow
4. Detecting leaked / never-resumed coroutines
5. Valgrind and AddressSanitizer notes
6. Tracing scheduler decisions
7. Common pitfalls

## 1. Recommended build flags

When hunting a bug, rebuild with:

```
make clean
CFLAGS="-O0 -g3 -fno-omit-frame-pointer -DCO_DEBUG=1" make
```

- `-O0 -g3` keeps every local visible in the debugger.
- `-fno-omit-frame-pointer` is important: our context switch assumes a
  normal frame chain, and omitted frame pointers can hide half the backtrace
  on some GCC / Clang versions.
- `-DCO_DEBUG=1` enables internal asserts in `src/sched.c` and
  `src/context.c` (parity check on stack alignment, magic-number guard
  around the stack canary, etc.).

## 2. Reading coroutine stacks in GDB

Each coroutine owns its own stack allocated on the heap (`malloc`-aligned
to 16 bytes). A live scheduler holds a `co_t*` for the currently running
coroutine and a linked list of ready and waiting ones. To inspect a
non-running coroutine:

```
(gdb) p sched.current
(gdb) p *sched.ready_head
(gdb) set $co = sched.ready_head
(gdb) p/x $co->ctx.rsp
(gdb) x/16gx $co->ctx.rsp
```

The top of that stack is where the coroutine will resume. Walking the saved
`rbp` chain from there gives you the frames, exactly as GDB would do for a
normal thread.

There is also a helper macro `co_print_backtrace(co)` available when the
library is built with `CO_DEBUG=1`.

## 3. Detecting stack overflow

Every coroutine stack is created with a small guard word
(`CO_STACK_CANARY = 0xDEADC0DECAFEBABE`) placed just below the low address
of the stack. The scheduler checks this word on each switch; if it has
changed, it aborts with a message pointing at the offending coroutine.

If you see `c-coroutine: stack canary corrupted`, the usual culprits are:

- A large local array (e.g. `char buf[1 << 20]`). Either shrink it or
  pass `CO_STACK_SIZE` when spawning.
- Deep recursion. Coroutine stacks are fixed-size, so unbounded recursion
  will overflow much earlier than on a normal thread.
- A buffer overrun elsewhere in the program that happens to land in the
  coroutine stack region.

## 4. Detecting leaked / never-resumed coroutines

When a coroutine is `co_wait`ed on something and the "something" never
completes, the scheduler will just exit with an empty ready list. In that
case `sched.num_coroutines` is still non-zero — this is your smoking gun.

Run with:

```
CO_DEBUG=1 ./your_program
```

and the scheduler will print a list of coroutines still alive at shutdown,
along with the file/line of their `co_spawn` call (captured via
`__FILE__` / `__LINE__` in `co_spawn`).

## 5. Valgrind and AddressSanitizer notes

Both tools work with `c-coroutine`, but they need a hint. Valgrind in
particular needs to know where the alternate stacks are:

```
valgrind --fair-sched=yes --main-stacksize=8388608 ./your_program
```

ASan does not need any special flag, but you must `export
ASAN_OPTIONS=detect_stack_use_after_return=0` — ASan's stack poisoning
interacts badly with coroutine switching (it assumes the stack belongs to
exactly one function at a time).

## 6. Tracing scheduler decisions

Build with `-DCO_TRACE=1` to get a log line for every switch, e.g.:

```
[co] switch 0x7f12a4000a00 -> 0x7f12a4000c80 (yield)
[co] switch 0x7f12a4000c80 -> 0x7f12a4000a00 (wake/io)
[co] retire 0x7f12a4000a00
```

This is invaluable when a coroutine appears to "hang". In practice the
scheduler is nearly always running — it's just picking the same coroutine
over and over because no one else is ready.

## 7. Common pitfalls

- **Forgetting to `co_return`.** Falling off the end of a coroutine is
  undefined behaviour in this library (the context-switch trampoline relies
  on a sentinel frame installed by `co_return`). Always return explicitly.
- **Yielding from a signal handler.** Don't. Signal handlers run on a
  different (OS-level) stack; yielding there corrupts the scheduler's
  invariants.
- **Calling blocking syscalls.** `read` or `write` on a non-`O_NONBLOCK`
  file descriptor will freeze the entire scheduler. Use the provided
  `co_read` / `co_write` wrappers, which internally register with the
  reactor and `co_yield` until I/O is ready.

If you find a debugging trick that is not covered here, please open an
issue with the `docs` label — a real reproducer is worth a thousand words.

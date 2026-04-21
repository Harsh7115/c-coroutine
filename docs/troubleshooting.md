# Troubleshooting Guide

This page catalogs the most common problems you will run into when building
or embedding **c-coroutine**, what causes them, and how to fix them.

> If your problem is not covered here, please open an issue with your OS,
> compiler version, `uname -m` output, and a minimal reproducer.

---

## 1. Build errors

### 1.1 `undefined reference to \`co_ctx_switch'`

**Cause.** The assembly backend file `src/ctx_asm.S` was not compiled in.
This usually happens when you try to build the library with a hand-rolled
build script that only globs `src/*.c`.

**Fix.** Include `.S` files too, or just use the provided `Makefile`:

```sh
make clean && make
```

If you are on a non-x86-64 host and the asm backend was skipped on purpose,
make sure the fallback path is enabled:

```sh
make COROUTINE_BACKEND=ucontext
```

### 1.2 `implicit declaration of function 'makecontext'`

**Cause.** You are on macOS, where `<ucontext.h>` is deprecated and hidden
behind a feature macro.

**Fix.** Define `_XOPEN_SOURCE` before including the header, or use the
asm backend (default on x86-64 Darwin).

### 1.3 Linker complains about duplicate `co_entry`

**Cause.** Older checkouts declared `co_entry` as `static` inside
`src/coroutine.c`, which prevents the assembly trampoline in
`src/ctx_asm.S` from referencing it.

**Fix.** Pull the latest `main`; this was fixed in commit
`fix: remove static from co_entry to restore external linkage for ASM`.

---

## 2. Runtime crashes

### 2.1 Segfault inside a coroutine the first time it yields

**Likely cause.** Stack overflow. The library does **not** install guard
pages, so blowing the stack just wanders into someone else's heap.

**Diagnosis.** Drop `valgrind --tool=memcheck` on the binary; an errant
write right at the top of a coroutine's stack is the giveaway.

**Fix.** Increase the per-coroutine stack via `co_create(fn, arg, size)`.
Default is 256 KB. Recursion-heavy workloads or anything using large
variable-length arrays may need 1 MB or more.

### 2.2 Double free / use-after-free in `co_free`

**Likely cause.** Calling `co_free` on a coroutine that is still in the
`CO_READY` or `CO_RUNNING` state.

**Fix.** Only free coroutines that have reached `CO_DONE`. Check with
`co_state(co) == CO_DONE` before freeing, or simply call `co_free` after
`co_run()` has returned --- at that point every coroutine is guaranteed
to have finished.

### 2.3 Program hangs forever inside `co_run`

**Likely cause.** A `co_await` cycle. If coroutine A awaits B and B
awaits A, neither will ever reach `CO_DONE` and the run queue empties.

**Fix.** Break the cycle. The `test_stress.c` suite uses a DAG-shaped
32-deep await chain; modelling your dependencies the same way avoids
cycles by construction.

---

## 3. Correctness surprises

### 3.1 `printf` output is interleaved in unexpected orders

**Explanation.** This is not a bug. The scheduler is FIFO round-robin but
each coroutine runs until it *voluntarily* yields, so a coroutine that
prints multiple lines before a `co_yield()` will emit them as a block.

If you need strict serialization, funnel all output through a single
logger coroutine and have workers send messages to it via an
`examples/channel.c`-style queue.

### 3.2 Global variables see "torn" updates

**Explanation.** Unlikely --- the library is single-threaded so there are
no concurrent writers. If you observe torn reads, you are probably
running two coroutines in two separate OS threads. That is not
supported.

### 3.3 `co_id()` returns 0 unexpectedly

**Explanation.** `co_id()` returns 0 outside of any coroutine. If you
hit this inside what you believed was coroutine code, check that you
actually reached that code via `co_run` --- a direct function call from
`main` will not install a coroutine context.

---

## 4. Performance surprises

### 4.1 "My workload is slower than regular function calls"

For sub-microsecond workloads the 14-movq context switch can dominate.
c-coroutine is aimed at workloads where each step does **some** work
(I/O, state machines, pipelines). For pure-compute hot loops, stay on
the normal call stack.

See `docs/performance-guide.md` for a full cost model.

### 4.2 Yields are mysteriously expensive

Likely your stack is huge and the CPU L1/L2 cache is thrashing. Try
smaller stacks (`co_create(fn, arg, 32 * 1024)`) and re-measure with
`benchmarks/bench_yield.c`.

---

## 5. Getting more help

1.  Re-run with `make tests` --- if the test suite fails, capture the
    failing test and include it in your issue.
2.  Build with `-g -O0 -fsanitize=address,undefined` for clearer stack
    traces.
3.  File an issue: <https://github.com/Harsh7115/c-coroutine/issues>.

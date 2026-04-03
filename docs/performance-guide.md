# Performance Guide

This document covers practical techniques for getting the best throughput and
latency out of **c-coroutine**.  It also explains when coroutines are the right
tool and when you should reach for threads or async I/O instead.

---

## Table of Contents

1. [Understanding the cost model](#1-understanding-the-cost-model)
2. [Stack sizing](#2-stack-sizing)
3. [Scheduler pressure and batch yields](#3-scheduler-pressure-and-batch-yields)
4. [Coroutines vs. threads vs. async I/O](#4-coroutines-vs-threads-vs-async-io)
5. [Cache and memory layout tips](#5-cache-and-memory-layout-tips)
6. [Profiling coroutine workloads](#6-profiling-coroutine-workloads)
7. [Benchmark results](#7-benchmark-results)

---

## 1. Understanding the cost model

A context switch in c-coroutine is a **cooperative register swap** — no kernel
involvement, no signal mask manipulation, no TLB flush.  The hand-written
x86-64 assembly saves/restores the six callee-saved registers plus the stack
pointer in roughly **10–15 ns** on a modern CPU (see
`benchmarks/context_switch.c`).

| Operation            | Approximate cost       |
|----------------------|------------------------|
| `co_yield()`         | ~12 ns (context switch)|
| `co_resume(co)`      | ~12 ns                 |
| `co_create()`        | ~2 µs (stack mmap)     |
| `co_destroy()`       | ~1 µs (stack munmap)   |
| `co_await(co)`       | 2× context switch      |

These numbers were measured on an AMD Ryzen 9 5950X at 3.4 GHz with
`-O2 -march=native`.  Your mileage will vary; always profile on your target
hardware.

> **Rule of thumb:** If the work between two `co_yield()` calls takes less than
> ~50 ns, the switch overhead becomes significant (>20 %).  In that case,
> consider batching more work per yield or switching to a purely event-driven
> model without coroutines.

---

## 2. Stack sizing

Each coroutine owns a private stack allocated with `mmap(MAP_ANONYMOUS)`.
Stack size is chosen at `co_create()` time and cannot grow dynamically.

### Choosing a stack size

| Workload                              | Recommended stack |
|---------------------------------------|-------------------|
| Simple leaf coroutine (no deep calls) | 8–16 KiB          |
| General-purpose coroutine             | 64 KiB (default)  |
| Deep call chains / recursive parsers  | 256 KiB – 1 MiB   |
| Coroutines that call into libraries   | ≥ 512 KiB         |

```c
/* Lean coroutine doing simple I/O event handling */
co_create(handler_fn, arg, 16 * 1024);

/* Coroutine that calls libc printf / snprintf extensively */
co_create(formatter_fn, arg, 256 * 1024);
```

### Guard pages

The allocator places a read-only guard page below every stack.  A stack
overflow will produce a SIGSEGV rather than silently corrupting the heap.
The guard page costs one extra `mmap()` call but no runtime overhead.

### Reusing stacks

If you spawn thousands of short-lived coroutines, the repeated
`mmap`/`munmap` round-trips dominate.  Consider a **coroutine pool**:

```c
/* Pseudo-code for a simple pool */
coroutine_t *pool[POOL_SIZE];
int          pool_head = 0;

coroutine_t *pool_get(co_fn fn, void *arg) {
    if (pool_head > 0) {
        coroutine_t *co = pool[--pool_head];
        co_reset(co, fn, arg);   /* hypothetical reset API */
        return co;
    }
    return co_create(fn, arg, 64 * 1024);
}

void pool_put(coroutine_t *co) {
    if (pool_head < POOL_SIZE) pool[pool_head++] = co;
    else                        co_destroy(co);
}
```

---

## 3. Scheduler pressure and batch yields

The FIFO scheduler walks the run-queue on every `co_yield()`.  With N
runnable coroutines, each full round-robin cycle costs O(N) context switches.

### Reducing unnecessary yields

Only call `co_yield()` when you genuinely have no more work to do in the
current turn.  Tight CPU-bound loops that yield on every iteration harm
throughput:

```c
/* Slow: yields every iteration */
for (int i = 0; i < N; i++) {
    process(items[i]);
    co_yield();
}

/* Better: yield every K items */
for (int i = 0; i < N; i++) {
    process(items[i]);
    if (i % 64 == 63) co_yield();
}
```

### Avoiding thundering herds

When many coroutines are waiting on the same event (e.g., a shared mutex),
releasing all of them simultaneously floods the run-queue.  Use a semaphore
with a max-permits policy or wake coroutines one at a time:

```c
/* Wake only the next waiter, not all */
co_sem_signal_one(&sem);  /* if your semaphore supports this */
```

---

## 4. Coroutines vs. threads vs. async I/O

| Criterion              | Coroutines        | pthreads          | epoll / io_uring   |
|------------------------|-------------------|-------------------|--------------------|
| Context-switch cost    | ~12 ns            | ~1–3 µs           | N/A                |
| Parallelism            | None (single core)| True (multi-core) | None (single core) |
| Stack per unit         | Configurable      | ~8 MiB default    | Minimal            |
| Synchronization needed | No (cooperative)  | Yes (locks/atomics)| Callback-based    |
| Debugging ease         | High              | Medium            | Low                |
| Best for               | I/O-bound, >1 K concurrent tasks | CPU-bound, latency-sensitive | Extreme concurrency |

**Use coroutines when:**
- All work happens on a single core (or one-thread-per-core sharding).
- You need 1 000s of lightweight concurrent tasks.
- Simplicity and debuggability matter more than raw parallelism.

**Prefer threads when:**
- Tasks genuinely benefit from parallel CPU execution.
- You need to saturate multiple cores with compute-heavy work.

**Prefer async I/O (epoll/io_uring) when:**
- You are handling hundreds of thousands of simultaneous connections.
- Per-connection state is minimal and can be stored in a small struct.

---

## 5. Cache and memory layout tips

### Packing coroutine state

If your coroutines share a large `arg` struct, place the hot fields first so
that the initial cache line covers everything the coroutine reads on each
activation:

```c
typedef struct {
    /* hot — read on every yield/resume */
    int          fd;
    uint32_t     flags;
    size_t       bytes_pending;

    /* cold — written once at creation */
    char         path[256];
    struct stat  st;
} ConnState;
```

### Coroutine arrays vs. linked lists

Storing active coroutine pointers in a contiguous array improves prefetcher
efficiency when iterating the run-queue, compared to a linked list where each
node can be scattered across the heap.

---

## 6. Profiling coroutine workloads

Standard profilers (perf, gprof) attribute all CPU time to whichever function
is currently on the CPU stack — they do not understand cooperative context
switches.

### Useful perf commands

```bash
# Record with call-graph (frame pointers must be preserved: -fno-omit-frame-pointer)
perf record -g -F 997 ./your_binary

# Flamegraph via Brendan Gregg's scripts
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

### Adding manual instrumentation

For fine-grained per-coroutine timing, wrap `co_yield()` and `co_resume()`
with `clock_gettime(CLOCK_MONOTONIC)` calls and accumulate per-coroutine
wall-clock time in a side-table keyed by coroutine ID.

---

## 7. Benchmark results

The `benchmarks/` directory contains micro-benchmarks you can run yourself:

```
benchmarks/
  context_switch.c    — raw yield/resume round-trip latency
  scheduler_scale.c   — throughput vs. number of runnable coroutines
  memory_pressure.c   — cost of creating/destroying large numbers of coroutines
```

Run them with:

```bash
make benchmarks
./build/bench_context_switch
./build/bench_scheduler_scale N=1000
```

Sample output on a Ryzen 9 5950X (single thread, `-O2`):

```
context_switch:      11.8 ns / round-trip
scheduler_scale N=100:   1.21 M yields/sec
scheduler_scale N=1000:  0.89 M yields/sec
scheduler_scale N=10000: 0.41 M yields/sec
```

Scheduler throughput degrades as the run-queue grows because each `co_yield()`
must traverse more of the FIFO list.  A work-stealing or priority-queue
scheduler could improve the N=10 000 case significantly.

# API Reference

This document describes every public symbol exported by **c-coroutine** (`include/coroutine.h`).

---

## Types

### `Coroutine`

```c
typedef struct Coroutine Coroutine;
```

Opaque handle representing a single coroutine. Allocated by `co_create` and freed by `co_destroy`. User code must not inspect or copy its internals.

---

## Lifecycle

### `co_create`

```c
Coroutine *co_create(size_t stack_size, CoFunc func, void *arg);
```

Allocate and initialize a new coroutine.

| Parameter    | Description |
| ------------ | ----------- |
| `stack_size` | Size of the private stack in bytes (minimum 4 096). |
| `func`       | Entry-point function with signature `void func(void *arg)`. |
| `arg`        | Opaque pointer forwarded to `func` when the coroutine first runs. |

**Returns** a pointer to the new `Coroutine`, or `NULL` on allocation failure.

---

### `co_destroy`

```c
void co_destroy(Coroutine *co);
```

Release all resources held by `co` (stack memory, control block). Behaviour is undefined if `co` is currently scheduled or running.

---

## Scheduling

### `co_spawn`

```c
void co_spawn(Coroutine *co);
```

Add `co` to the run queue so that `co_run` will execute it. A coroutine must be spawned exactly once before it can run.

---

### `co_run`

```c
void co_run(void);
```

Enter the scheduler loop and run all spawned coroutines until the queue is empty. Returns to the caller only when no runnable coroutines remain.

---

## Cooperative control

### `co_yield`

```c
void co_yield(void);
```

Suspend the currently running coroutine and transfer control back to the scheduler, which picks the next runnable coroutine in FIFO order. The calling coroutine is automatically re-queued.

---

### `co_await`

```c
void co_await(Coroutine *co);
```

Suspend the current coroutine until `co` has finished executing.

| Parameter | Description |
| --------- | ----------- |
| `co`      | Coroutine to wait for. Must have been spawned. |

---

## Type aliases

```c
typedef void (*CoFunc)(void *arg);
```

Signature required for the entry-point function passed to `co_create`.

---

## Error handling

All functions that can fail return `NULL` (for pointer-returning functions) or have undefined behaviour when passed invalid arguments. Check return values and avoid double-spawning or double-destroying a coroutine.

---

## Thread safety

c-coroutine is **not** thread-safe. All coroutines must be created, spawned, and driven from a single thread.

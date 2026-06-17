# Integrating c-coroutine into an Existing C Project

This guide covers embedding **c-coroutine** into an existing C project — from
linking the library to common patterns and pitfalls.

---

## 1. Adding the Library

### Option A — Git submodule (recommended)

```bash
git submodule add https://github.com/Harsh7115/c-coroutine vendor/c-coroutine
git submodule update --init
make -C vendor/c-coroutine
```

Update your build system:

```makefile
CFLAGS  += -Ivendor/c-coroutine/include
LDFLAGS += vendor/c-coroutine/lib/libcoroutine.a
```

### Option B — Vendored sources

Copy these files directly into your source tree if you prefer no submodule:

```
include/coroutine.h
src/coroutine.c
src/ctx_switch_amd64.S   # or ctx_switch_ucontext.c for non-x86-64
```

---

## 2. Integration Checklist

| Step | What to do |
|------|-----------|
| Include header | `#include "coroutine.h"` in any file that creates or yields coroutines |
| One scheduler per thread | `co_run()` must be called from exactly one OS thread |
| No blocking syscalls inside coroutines | Replace `sleep()`, blocking `read()`, `pthread_mutex_lock()` with cooperative alternatives |
| Stack size | Default 256 KB; pass a custom `stack_size` to `co_create()` if needed |
| Lifetime | Call `co_free()` after the coroutine reaches `CO_DONE` |

---

## 3. Common Patterns

### 3.1 Event-loop bridge

Drive coroutines from inside an existing `epoll`/`select` loop:

```c
while (running) {
    int n = epoll_wait(epfd, events, MAX_EVENTS, timeout_ms);
    for (int i = 0; i < n; i++) {
        Connection *conn = events[i].data.ptr;
        if (!conn->co)
            conn->co = co_create(handle_connection, conn, 0);
        // mark ready and let co_run() resume it on the next tick
    }
    co_run();   // resumes all READY coroutines once
}
```

### 3.2 Thread-pool hand-off

Dedicate one worker thread to coroutine execution:

```c
void *coroutine_thread(void *arg) {
    WorkQueue *q = arg;
    while (!q->shutdown) {
        Task *t = dequeue(q);          // blocks on mutex/condvar
        Coroutine *co = co_create(t->fn, t->arg, 0);
        co_run();
        co_free(co);
        task_free(t);
    }
    return NULL;
}
```

All coroutine API calls must stay on this one thread — the library is
**single-threaded by design**.

### 3.3 Incremental migration from pthreads

Migrate one handler at a time without rewriting the whole codebase:

1. Swap `pthread_create(handle_request, conn)` for
   `co_create(handle_request, conn, 0)`.
2. Replace per-thread mutex pairs that only guard data on this thread with
   flag variables and `co_yield()`.
3. Leave mutexes that protect genuinely shared data untouched — those guard
   cross-thread access that coroutines cannot replace.

---

## 4. Stack Overflow Protection

The library does **not** install guard pages by default (a deliberate
zero-dependency design choice).  For production code, allocate an `mmap`
region and mark the bottom page `PROT_NONE` before passing the stack to
`co_create`.  See [porting-guide.md](porting-guide.md) for a full example.

---

## 5. Non-x86-64 Targets

The library selects its context-switch backend at compile time:

| Target | Backend |
|--------|---------|
| Linux / macOS x86-64 | Hand-written `co_ctx_switch` in assembly |
| Everything else | POSIX `makecontext`/`swapcontext` |

Force the portable backend:

```bash
make ARCH=generic -C vendor/c-coroutine
```

---

## 6. Debugging

- **AddressSanitizer**: the asm backend is ASan-compatible; register the
  alternate stack with `__sanitizer_start_switch_fiber` before each context
  switch.
- **Log correlation**: use `co_id()` in log statements to identify which
  coroutine produced each line.
- **GDB**: see [debugging.md](debugging.md) for a pretty-printer that displays
  the run-queue and per-coroutine state.

---

## See Also

- [api_reference.md](api_reference.md)
- [performance-guide.md](performance-guide.md)
- [troubleshooting.md](troubleshooting.md)
- [porting-guide.md](porting-guide.md)

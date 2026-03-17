# c-coroutine

[![CI](https://github.com/Harsh7115/c-coroutine/actions/workflows/ci.yml/badge.svg)](https://github.com/Harsh7115/c-coroutine/actions/workflows/ci.yml)

A cooperative userspace coroutine (fiber) library written in C.

Coroutines share a single OS thread and cooperatively yield the CPU — no
kernel involvement, no race conditions, no locks.  The scheduler is a simple
FIFO round-robin; `co_yield()` suspends the current coroutine and resumes
the next READY one; `co_await()` suspends until a specific coroutine
finishes.

Two context-switch backends are provided:

| Backend | How | When used |
|---|---|---|
| **asm** (default on x86-64) | Hand-written `co_ctx_switch` in x86-64 assembly — saves/restores the 7 callee-saved registers mandated by the System V AMD64 ABI | Linux / macOS x86-64 |
| **ucontext** (fallback) | POSIX `makecontext` / `swapcontext` | All other architectures |

The assembly path is selected automatically at compile time via `uname -m`.

---

## How it works

### Context switching (x86-64 asm backend)

The System V AMD64 ABI classifies registers into two groups:

- **Caller-saved** (`%rax`, `%rcx`, `%rdx`, `%rsi`, `%rdi`, `%r8–%r11`): the
  callee may clobber them freely; the compiler already saves/restores them
  around any call site that needs them.
- **Callee-saved** (`%rbx`, `%rbp`, `%r12–%r15`): the callee must preserve
  them across the call.

A context switch therefore only needs to snapshot 7 registers plus `%rsp`
(stack pointer).  The instruction pointer is handled implicitly: we push the
return address onto the outgoing stack before switching, so `ret` in the asm
stub lands execution back at the call site.

```
co_ctx_switch(AsmCtx *from, const AsmCtx *to):
    # save callee-saved regs of the outgoing coroutine
    movq %rsp, 0x00(%rdi)
    movq %rbp, 0x08(%rdi)  ...

    # restore callee-saved regs of the incoming coroutine
    movq 0x00(%rsi), %rsp
    movq 0x08(%rsi), %rbp  ...

    ret    # pops the return address from the restored %rsp → resumes execution
```

For a freshly created coroutine, the "return address" on its stack points to
`co_trampoline`, a two-instruction stub that pops the `Coroutine*` into `%rdi`
(the first integer argument register) and jumps to the C-level entry function.

### Scheduler

```
┌──────────────────────────────────┐
│           co_run() loop          │
│  while (co = q_pop()) != NULL:   │
│      resume(co)   ──────────────►│─── co_ctx_switch(main, co)
│      ◄───────────────────────────│─── co_ctx_switch(co, main)  [yield/done]
└──────────────────────────────────┘
```

- `co_run()` drives a FIFO run-queue until all coroutines reach `CO_DONE`.
- `co_yield()` moves the current coroutine from `CO_RUNNING` → `CO_READY`,
  re-enqueues it, and switches back to the scheduler context.
- `co_await(other)` moves the current coroutine to `CO_WAITING`.  When
  `other` finishes, it calls `wakeup_waiters()`, which promotes waiting
  coroutines to `CO_READY`.

### Stack layout for a new coroutine (x86-64)

```
high address  ┌──────────────────────┐  ← stack base (stack + stack_size)
              │   Coroutine *co      │  ← co_trampoline pops this → %rdi
              │   &co_trampoline     │  ← first `ret` in co_ctx_switch jumps here
              └──────────────────────┘  ← ctx.rsp
low address
```

---

## API

```c
// coroutine.h

typedef struct Coroutine Coroutine;
typedef enum { CO_READY, CO_RUNNING, CO_WAITING, CO_DONE } CoState;

Coroutine *co_create(void (*fn)(void *), void *arg, size_t stack_size);
void       co_free(Coroutine *co);

void    co_yield(void);           // yield to scheduler
void    co_await(Coroutine *co);  // suspend until co finishes
void    co_run(void);             // start scheduler; returns when all done

int     co_id(void);              // ID of current coroutine (0 if not in one)
CoState co_state(const Coroutine *co);
```

`stack_size = 0` uses the library default (256 KB).

---

## Quick start

```c
#include "include/coroutine.h"
#include <stdio.h>

static void hello(void *arg) {
    printf("[%d] hello\n", co_id());
    co_yield();
    printf("[%d] world\n", co_id());
}

int main(void) {
    Coroutine *a = co_create(hello, NULL, 0);
    Coroutine *b = co_create(hello, NULL, 0);
    co_run();
    co_free(a);
    co_free(b);
}
```

```
make
gcc -Iinclude examples/hello.c lib/libcoroutine.a -o hello
./hello
```

---

## Building

```bash
git clone https://github.com/Harsh7115/c-coroutine
cd c-coroutine
make          # builds lib/libcoroutine.a
make tests    # builds and runs all unit + stress tests
make examples # builds and runs the Fibonacci generator example
```

Requires GCC (or Clang), GNU Make.  No external dependencies.

---

## Tests

```
tests/
  test_basic.c     — co_create, co_yield, co_await, co_id, co_state (15 cases)
  test_pipeline.c  — producer → squarer → consumer ring-buffer pipeline
  test_stress.c    — 200 coroutines × random yields; 32-deep await chain
```

---

## Examples

```
examples/
  generator.c  — lazy Fibonacci sequence via cooperative generator/consumer
```

---

## Design notes

- **Zero dependencies** — only `<ucontext.h>` (fallback path) or pure asm.
- **Single-threaded by design** — cooperative scheduling eliminates data races.
- **O(1) context switch** — asm path: 14 `movq` instructions + `ret`.
- **Wakeup scan is O(n)** — acceptable for expected scale.
- **Stack overflow is not detected** — add `mprotect` guard pages for production use.

---

## License

MIT

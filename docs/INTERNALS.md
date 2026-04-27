# c-coroutine Internals

This document goes deeper than the README on how the library actually
schedules and switches coroutines. It is intended for contributors who
want to extend the scheduler, port the assembly backend to a new ABI, or
debug a corrupted-context crash.

## 1. The two backends

```
                    +-------------------+
                    |    coroutine.h    |
                    +---------+---------+
                              |
            +-----------------+-----------------+
            |                                   |
   +--------v--------+                +---------v--------+
   |   ctx_asm.S     |                |   ctx_ucontext.c |
   |  (x86-64 Linux  |                |   (POSIX makecon |
   |   / macOS)      |                |    text fallback)|
   +-----------------+                +------------------+
```

`make` selects the backend automatically:

```make
ifeq ($(shell uname -m),x86_64)
    BACKEND := ctx_asm.S
else
    BACKEND := ctx_ucontext.c
    CFLAGS  += -DCO_USE_UCONTEXT
endif
```

The fallback path is intentionally a thin wrapper: `co_ctx_init` calls
`makecontext`, `co_ctx_switch` calls `swapcontext`. It exists to keep
CI green on ARM macOS runners and to let us cross-check the asm path
against a known-good reference.

## 2. The seven registers

System V AMD64 (Linux, macOS, BSDs) classifies the integer registers as
follows:

| Register      | Class         | Saved by |
|---------------|---------------|----------|
| %rax          | caller-saved  | caller   |
| %rcx, %rdx    | caller-saved  | caller   |
| %rsi, %rdi    | caller-saved  | caller   |
| %r8 .. %r11   | caller-saved  | caller   |
| %rbx          | callee-saved  | callee   |
| %rbp          | callee-saved  | callee   |
| %r12 .. %r15  | callee-saved  | callee   |

Caller-saved registers are already spilled to the stack by GCC around
any call site that needs to keep them live, so `co_ctx_switch` does not
have to touch them. We only persist the six callee-saved registers plus
`%rsp`. Floating-point state (`%xmm0..%xmm15`) is also caller-saved on
this ABI, so it is similarly safe to omit.

## 3. The trampoline

A freshly created coroutine has never executed before, so it cannot have
a saved `%rip`. We synthesise one by hand-rolling the stack:

```
high address
+--------------------+   <- stack base
| Coroutine *co      |   <- co_trampoline pops this -> %rdi
+--------------------+
| &co_trampoline     |   <- ret in co_ctx_switch jumps here
+--------------------+   <- ctx.rsp at create time
low address
```

`co_trampoline` is two instructions:

```asm
co_trampoline:
    popq %rdi           # 1st arg: Coroutine *self
    jmp  co_entry       # never returns
```

`co_entry` is a regular C function that calls the user-supplied
`fn(arg)`, marks the coroutine `CO_DONE`, wakes any awaiters, and
finally yields back to the scheduler one last time. It must never
return into the trampoline — there is nothing on the stack below it
except poisoned bytes.

## 4. The run-queue

The scheduler is a simple FIFO. Each `co_yield()` re-enqueues the
caller at the tail; `co_run()` pops from the head. This gives strict
round-robin behaviour and keeps starvation impossible as long as every
coroutine yields.

We deliberately do not use a priority queue. The whole point of a
cooperative scheduler is that it is predictable: with FIFO + cooperative
yielding, the order of execution is a deterministic function of the
order of `co_create` and `co_yield` calls. That property is invaluable
when reproducing race-y bugs in higher-level code that happens to run on
top of the library.

## 5. Awaiting

`co_await(other)` is implemented as:

```c
void co_await(Coroutine *other) {
    if (other->state == CO_DONE) return;
    Coroutine *self = current;
    self->state = CO_WAITING;
    self->awaiting = other;
    co_yield();   // does not return until other completes
}
```

When a coroutine reaches `CO_DONE`, `co_entry` walks the global
coroutine list and promotes anyone with `awaiting == self` back to
`CO_READY`. The scan is O(n) in the number of live coroutines; for the
expected scale (hundreds, not millions) this is cheaper than maintaining
per-coroutine waiter lists.

## 6. Stack sizing

The default stack size is 256 KB. That is large enough to absorb a
moderately deep recursive descent through libc (printf, malloc) without
risk, and small enough that even 1000 concurrent coroutines fit in
256 MB of address space. Production deployments should:

* Pass an explicit `stack_size` matched to the workload.
* Add a `PROT_NONE` guard page below each stack with `mprotect` so a
  stack overflow becomes a SIGSEGV instead of silent corruption.
* Use `MADV_DONTNEED` to release physical pages of unused stack tails.

None of those are done by the library today; they are listed as future
work in TODO.md.

## 7. What is *not* supported

* Preemption. There is no timer interrupt; a coroutine that loops
  without yielding will hang the whole program.
* Multi-threading. `current` and the run-queue are plain globals.
* Stack growth. Stacks are fixed-size and contiguous.
* Cancellation. Once a coroutine has started, it must run to completion
  on its own. Add a cancellation flag and check it at yield points if
  you need this.

These are deliberate omissions, not bugs.

# Memory Model

This document describes the memory model used by the c-coroutine library, focusing on
stack management, the context-switch ABI, and what guarantees callers can rely on.

## Stack layout

Each coroutine is allocated its own private stack. The default stack size is 64 KiB
and can be overridden via `co_create_with_stack(size)`. Stacks are allocated on the
heap with `mmap(2)` so they can be guarded with a single zero-permission page at the
low end. Touching the guard page raises `SIGSEGV` instead of silently corrupting the
allocation that lives below it.

```
    high addresses
   +--------------+
   | initial RSP  |  <- stack base, aligned to 16 bytes
   +--------------+
   | red zone     |  <- 128 bytes reserved by the SysV ABI
   +--------------+
   | live frames  |
   +--------------+
   | unused stack |
   +--------------+
   | guard page   |  <- PROT_NONE, traps on overflow
   +--------------+
    low addresses
```

Stacks are never resized once a coroutine has been created. Deep recursion or large
local arrays must be sized to fit, or the coroutine should be created with a larger
stack from the start.

## Saved register set

The hand-written x86-64 context switch saves only the callee-saved registers required
by the SysV AMD64 ABI: `rbx`, `rbp`, `r12`, `r13`, `r14`, `r15`, plus the new
stack pointer. Caller-saved registers (the argument and scratch registers) are the
caller's responsibility — they will already have been spilled by the C compiler at
the call site of `co_yield`.

The FPU control word and MXCSR are *not* saved or restored. This is a deliberate
choice: most code never modifies them, and saving them on every switch would more
than double the cost of the fast path. If your code mutates rounding modes or
exception masks, save them yourself before yielding.

## Switch ordering and visibility

`co_yield` and `co_resume` are full compiler barriers — they emit an `asm volatile`
clobber that prevents the compiler from reordering loads or stores across the switch.
On x86-64 they are also natural processor fences for ordinary memory traffic, so a
write performed before `co_yield` is visible to the next coroutine that observes the
shared location.

This does *not* extend to non-temporal stores or weakly-ordered devices. If you are
mixing coroutines with SSE streaming stores or memory-mapped I/O, insert your own
`mfence` or `sfence` as appropriate.

## Lifetime rules

Pointers into a coroutine's stack are valid only while that coroutine exists. Once
`co_destroy` is called the backing `mmap` region is freed and any pointer into it
becomes dangling. The scheduler will refuse to resume a destroyed coroutine and
returns `CO_ERR_DEAD`.

Pointers into the heap, into another coroutine's stack, or into globals are all
valid as long as the underlying object lives — coroutine boundaries do not change
the usual C lifetime rules.

## See also

- `docs/scheduler.md` for the FIFO scheduler internals
- `examples/timer.c` for a realistic coroutine-driven timer
- `tests/test_stack_overflow.c` for the guard-page behaviour

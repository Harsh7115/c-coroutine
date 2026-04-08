# Porting Guide

This document explains how to add a new CPU architecture backend to c-coroutine.
The library already ships two backends:

| Backend | File | Used when |
|---------|------|-----------|
| x86-64 assembly | `src/ctx_asm.S` | `uname -m` returns `x86_64` |
| POSIX `ucontext` | `src/ctx_ucontext.c` | all other architectures |

If you are targeting **AArch64 (ARM64)**, **RISC-V**, **MIPS**, or any other
architecture, follow the steps below to add a native assembly backend that avoids
the overhead of the `ucontext` fallback.

---

## 1. Understand what a context switch needs to save

A context switch must preserve all registers the **C ABI classifies as
callee-saved**.  The table below lists the registers for the most common
architectures:

| Architecture | Callee-saved integer registers | Frame / stack pointer |
|---|---|---|
| x86-64 (System V) | `rbx`, `rbp`, `r12`–`r15` | `rsp` |
| AArch64 (AAPCS64) | `x19`–`x28`, `x29` (FP) | `sp`, `lr` (`x30`) |
| RISC-V (RISC-V ELF psABI) | `s0`–`s11`, `ra` | `sp` |
| MIPS32 (o32) | `s0`–`s7`, `s8` (fp) | `sp`, `ra` |

> **Floating-point / SIMD registers**: only save them if your coroutine bodies
> use FP/SIMD code *and* the ABI says they are callee-saved.  For most
> architectures the FP callee-saved registers follow the same numbering
> convention as the integer ones.

---

## 2. Define the context struct

Add a new struct to `include/coroutine_ctx.h` (or extend the existing
`#if defined(__x86_64__)` chain):

```c
/* AArch64 example */
#elif defined(__aarch64__)
typedef struct {
    uint64_t x19, x20, x21, x22, x23, x24;
    uint64_t x25, x26, x27, x28;
    uint64_t fp;   /* x29 */
    uint64_t lr;   /* x30 — return address */
    uint64_t sp;
} AsmCtx;
```

The struct members must appear in the same order you will `str`/`ldr` them
in the assembly stub, because the asm code indexes them by fixed byte offsets.

---

## 3. Write the assembly stub

Create `src/ctx_ARCH.S` (e.g. `src/ctx_aarch64.S`).

### AArch64 example

```asm
/* ctx_aarch64.S — co_ctx_switch(AsmCtx *from, const AsmCtx *to)
 * Arguments: x0 = from, x1 = to
 */
    .text
    .global co_ctx_switch
    .type   co_ctx_switch, %function

co_ctx_switch:
    /* ── save outgoing context ──────────────────────────────────── */
    stp  x19, x20, [x0, #0]
    stp  x21, x22, [x0, #16]
    stp  x23, x24, [x0, #32]
    stp  x25, x26, [x0, #48]
    stp  x27, x28, [x0, #64]
    stp  x29, x30, [x0, #72]   /* fp, lr */
    mov  x9,  sp
    str  x9,  [x0, #88]

    /* ── restore incoming context ───────────────────────────────── */
    ldp  x19, x20, [x1, #0]
    ldp  x21, x22, [x1, #16]
    ldp  x23, x24, [x1, #32]
    ldp  x25, x26, [x1, #48]
    ldp  x27, x28, [x1, #64]
    ldp  x29, x30, [x1, #72]   /* fp, lr */
    ldr  x9,  [x1, #88]
    mov  sp,  x9

    ret                         /* branches to lr of incoming context */
    .size co_ctx_switch, .-co_ctx_switch
```

### Stack layout for a newly created coroutine

When `co_create` initialises a fresh coroutine it must fake a context that,
when first resumed, begins executing the coroutine body.  The exact layout
depends on the calling convention:

- **x86-64**: push the return address onto the stack (`ctx.rsp -= 8`);
  set `*(uint64_t *)ctx.rsp = (uint64_t)co_trampoline`.
- **AArch64**: store `co_trampoline` in `ctx.lr`; set `ctx.sp` to
  the top of the allocated stack (8-byte aligned).
- **RISC-V**: store `co_trampoline` in `ctx.ra`; set `ctx.sp`.

`co_trampoline` is a small C function that receives the `Coroutine *`
pointer (via the first argument register: `%rdi` / `x0` / `a0`) and calls
the user-supplied function.

---

## 4. Hook the new backend into the build system

In `Makefile` (or `CMakeLists.txt`) add an `ifeq` / `elseif` block:

```makefile
ARCH := $(shell uname -m)
ifeq ($(ARCH),x86_64)
    CTX_SRC := src/ctx_asm.S
else ifeq ($(ARCH),aarch64)
    CTX_SRC := src/ctx_aarch64.S
else
    CTX_SRC := src/ctx_ucontext.c
endif
```

---

## 5. Test your backend

Run the full test suite on the target hardware:

```sh
make clean
make tests
```

All 15 unit tests in `tests/test_basic.c` plus the stress test
(`tests/test_stress.c`) must pass before submitting a port.

If you are cross-compiling, use QEMU user-mode emulation:

```sh
# AArch64 cross-build + QEMU run
make CC=aarch64-linux-gnu-gcc ARCH_OVERRIDE=aarch64
qemu-aarch64 -L /usr/aarch64-linux-gnu ./tests/test_basic
```

---

## 6. Submit a pull request

1. Add your `src/ctx_ARCH.S` file.
2. Update `include/coroutine_ctx.h` with the new `AsmCtx` variant.
3. Update `Makefile` (and `CMakeLists.txt` if present).
4. Add a short entry to `CHANGELOG.md`.
5. Open a PR with the title `feat: add <arch> asm context-switch backend`.

Please include benchmark numbers comparing your asm backend against the
`ucontext` fallback (see `benchmarks/` for the harness).

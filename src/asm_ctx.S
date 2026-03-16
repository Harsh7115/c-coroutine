/*
 * asm_ctx.S — x86-64 System V ABI context switch
 *
 * The System V AMD64 ABI partitions general-purpose registers into two sets:
 *
 *   Caller-saved  (callee may clobber):  %rax %rcx %rdx %rsi %rdi
 *                                        %r8-%r11, %xmm0-%xmm15
 *   Callee-saved  (callee must preserve): %rbx %rbp %r12-%r15
 *
 * A context switch only needs to save/restore the callee-saved set plus
 * %rsp (stack pointer) and the return address (instruction pointer), because
 * the calling convention already guarantees the caller-saved registers are
 * dead at any call site.
 *
 * AsmCtx layout (matches struct in coroutine.c):
 *   offset 0x00  rsp
 *   offset 0x08  rbp
 *   offset 0x10  rbx
 *   offset 0x18  r12
 *   offset 0x20  r13
 *   offset 0x28  r14
 *   offset 0x30  r15
 *
 * ─────────────────────────────────────────────────────────────────────────
 * co_ctx_switch(AsmCtx *from, const AsmCtx *to)
 *
 *   Saves current registers into *from, restores from *to, then returns
 *   into the execution context that was saved in *to.
 *
 * Arguments (System V):
 *   %rdi = from  (AsmCtx *)
 *   %rsi = to    (const AsmCtx *)
 * ─────────────────────────────────────────────────────────────────────────
 */

#if defined(__x86_64__)

.text
.globl co_ctx_switch
.type  co_ctx_switch, @function
co_ctx_switch:
    /* ── Save caller's callee-saved registers into *from (%rdi) ── */
    movq %rsp, 0x00(%rdi)
    movq %rbp, 0x08(%rdi)
    movq %rbx, 0x10(%rdi)
    movq %r12, 0x18(%rdi)
    movq %r13, 0x20(%rdi)
    movq %r14, 0x28(%rdi)
    movq %r15, 0x30(%rdi)

    /* ── Restore callee-saved registers from *to (%rsi) ── */
    movq 0x00(%rsi), %rsp
    movq 0x08(%rsi), %rbp
    movq 0x10(%rsi), %rbx
    movq 0x18(%rsi), %r12
    movq 0x20(%rsi), %r13
    movq 0x28(%rsi), %r14
    movq 0x30(%rsi), %r15

    /*
     * `ret` pops the top of the restored stack into %rip.
     * For a coroutine mid-execution this is the return address pushed by the
     * `call co_ctx_switch` inside co_yield/co_await.
     * For a freshly created coroutine this is &co_trampoline (see below).
     */
    ret
.size co_ctx_switch, .-co_ctx_switch


/*
 * co_trampoline — entry point for a newly created coroutine
 *
 * Stack layout on first resume (see coroutine.c co_create):
 *   [rsp+0]  &co_trampoline   ← popped by `ret` in co_ctx_switch → %rip
 *   [rsp+8]  Coroutine *co    ← we pop this into %rdi, then ret into co_entry
 */
.globl co_trampoline
.type  co_trampoline, @function
co_trampoline:
    pop  %rdi                    /* Coroutine *co → first arg register */
    jmp  co_entry                /* tail-call; co_entry(co) */
.size co_trampoline, .-co_trampoline

#elif defined(__aarch64__)

/*
 * AArch64 (ARM64) fallback — saved for completeness.
 * The coroutine.c ucontext path is used on this architecture.
 */

#else
/* Architecture not supported by the asm backend; coroutine.c falls back
 * to the ucontext path automatically via #if USE_ASM_CTX. */
#endif

/* Mark the stack as non-executable (Linux/ELF requirement). */
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack, "", @progbits
#endif

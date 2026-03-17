/*
 * coroutine.c — cooperative coroutine scheduler
 *
 * Context switching strategy
 * --------------------------
 * We maintain two parallel implementations and select at compile time:
 *
 *   USE_ASM_CTX  (default on x86-64 Linux/macOS)
 *     A hand-written context switch in src/asm_ctx.S that saves and restores
 *     only the callee-saved registers mandated by the System V AMD64 ABI:
 *       %rbp  %rbx  %r12  %r13  %r14  %r15  %rsp
 *     plus the instruction pointer (via a pushed return address).
 *     This is exactly what a real fiber runtime does (e.g., Boost.Context,
 *     Go's runtime·gogo, Linux kfibers).
 *
 *   UCONTEXT_CTX  (fallback / other architectures)
 *     Uses POSIX ucontext_t (makecontext / swapcontext).  More portable but
 *     carries extra syscall overhead on some platforms.
 *
 * Scheduler
 * ---------
 * A simple FIFO run-queue holds pointers to READY coroutines.
 * co_run() round-robins through it; co_yield() re-enqueues the current
 * coroutine; co_await() removes it until the awaited coroutine is DONE.
 *
 * Thread safety: this library is single-threaded by design.
 */

#include "coroutine.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── platform detection ─────────────────────────────────────────────────── */
#if defined(__x86_64__) && (defined(__linux__) || defined(__APPLE__))
#  define USE_ASM_CTX 1
#else
#  define USE_ASM_CTX 0
#endif

#if USE_ASM_CTX
/* Declared in asm_ctx.S */
typedef struct {
    uint64_t rsp;   /* must be first — asm_ctx.S indexes from offset 0 */
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} AsmCtx;

extern void co_ctx_switch(AsmCtx *from, const AsmCtx *to);  /* asm_ctx.S */
#else
#  include <ucontext.h>
#endif

/* ── tunables ────────────────────────────────────────────────────────────── */
#define DEFAULT_STACK_SIZE  (256u * 1024u)   /* 256 KB per coroutine        */
#define MAX_COROUTINES      512              /* hard cap; extend if needed  */
#define QUEUE_CAP           (MAX_COROUTINES * 2)

/* ── coroutine struct ────────────────────────────────────────────────────── */
struct Coroutine {
    int      id;
    CoState  state;

    void   (*fn)(void *);
    void    *arg;

    char    *stack;
    size_t   stack_size;

    /* When state == CO_WAITING, this is the coroutine we are blocked on. */
    Coroutine *awaiting;

#if USE_ASM_CTX
    AsmCtx  ctx;
#else
    ucontext_t ctx;
#endif
};

/* ── scheduler globals ───────────────────────────────────────────────────── */
static struct {
    Coroutine *all[MAX_COROUTINES];   /* every coroutine ever created       */
    int        count;                  /* length of `all`                    */

    /* FIFO run-queue (circular buffer of Coroutine*) */
    Coroutine *queue[QUEUE_CAP];
    int        q_head;
    int        q_tail;
    int        q_size;

    Coroutine *running;   /* currently executing coroutine, NULL = main     */

#if USE_ASM_CTX
    AsmCtx    main_ctx;   /* saved registers of the scheduler / main thread */
#else
    ucontext_t main_ctx;
#endif
} S;

/* ── run-queue helpers ───────────────────────────────────────────────────── */
static void q_push(Coroutine *co) {
    assert(S.q_size < QUEUE_CAP && "run-queue overflow");
    S.queue[S.q_tail] = co;
    S.q_tail = (S.q_tail + 1) % QUEUE_CAP;
    S.q_size++;
}

static Coroutine *q_pop(void) {
    if (S.q_size == 0) return NULL;
    Coroutine *co = S.queue[S.q_head];
    S.q_head = (S.q_head + 1) % QUEUE_CAP;
    S.q_size--;
    return co;
}

/* ── wakeup: move any coroutine waiting on `done` back to READY ─────────── */
static void wakeup_waiters(const Coroutine *done) {
    for (int i = 0; i < S.count; i++) {
        Coroutine *co = S.all[i];
        if (co->state == CO_WAITING && co->awaiting == done) {
            co->state    = CO_READY;
            co->awaiting = NULL;
            q_push(co);
        }
    }
}

/* ── trampoline (called when a new coroutine starts) ────────────────────── */
#if USE_ASM_CTX
void co_entry(Coroutine *co) {
    co->fn(co->arg);

    co->state = CO_DONE;
    wakeup_waiters(co);

    /* Return to scheduler. */
    co_ctx_switch(&co->ctx, &S.main_ctx);

    /* Unreachable, but keeps the compiler happy. */
    abort();
}
#else
static void co_entry_uc(uint32_t hi, uint32_t lo) {
    Coroutine *co = (Coroutine *)((uintptr_t)hi << 32 | (uintptr_t)lo);
    co->fn(co->arg);

    co->state = CO_DONE;
    wakeup_waiters(co);

    swapcontext(&co->ctx, &S.main_ctx);
    abort();
}
#endif

/* ── public API ─────────────────────────────────────────────────────────── */

Coroutine *co_create(void (*fn)(void *), void *arg, size_t stack_size) {
    assert(fn && "fn must not be NULL");
    assert(S.count < MAX_COROUTINES && "too many coroutines");

    if (stack_size == 0) stack_size = DEFAULT_STACK_SIZE;

    Coroutine * volatile co = calloc(1, sizeof(Coroutine));
    if (!co) return NULL;

    co->stack = malloc(stack_size);
    if (!co->stack) { free(co); return NULL; }

    co->id         = S.count + 1;
    co->state      = CO_READY;
    co->fn         = fn;
    co->arg        = arg;
    co->stack_size = stack_size;
    co->awaiting   = NULL;

#if USE_ASM_CTX
    extern void co_trampoline(void);  /* defined in asm_ctx.S */

    char     *stack_top = co->stack + stack_size;
    uintptr_t top = (uintptr_t)stack_top;
    top &= ~(uintptr_t)15;
    uint64_t *sp = (uint64_t *)top;

    *(--sp) = (uint64_t)(uintptr_t)co;
    *(--sp) = (uint64_t)(uintptr_t)co_trampoline;

    memset(&co->ctx, 0, sizeof(co->ctx));
    co->ctx.rsp = (uint64_t)(uintptr_t)sp;
#else
    getcontext(&co->ctx);
    co->ctx.uc_stack.ss_sp    = co->stack;
    co->ctx.uc_stack.ss_size  = stack_size;
    co->ctx.uc_link           = NULL;

    uintptr_t ptr = (uintptr_t)co;
    makecontext(&co->ctx, (void (*)(void))co_entry_uc, 2,
                (uint32_t)(ptr >> 32), (uint32_t)(ptr & 0xffffffffu));
#endif

    S.all[S.count++] = co;
    q_push(co);
    return co;
}

void co_free(Coroutine *co) {
    assert(co && "NULL coroutine");
    assert(co->state == CO_DONE && "co_free on non-finished coroutine");
    free(co->stack);
    free(co);
}

/* Switch from current coroutine (or main) into `next`. */
static void resume(Coroutine *next) {
    next->state   = CO_RUNNING;
    S.running     = next;

#if USE_ASM_CTX
    co_ctx_switch(&S.main_ctx, &next->ctx);
#else
    swapcontext(&S.main_ctx, &next->ctx);
#endif

    S.running = NULL;
}

void co_run(void) {
    Coroutine *co;
    while ((co = q_pop()) != NULL) {
        assert(co->state == CO_READY);
        resume(co);
    }
}

void co_yield(void) {
    Coroutine *co = S.running;
    assert(co && "co_yield called outside a coroutine");
    co->state = CO_READY;
    q_push(co);
#if USE_ASM_CTX
    co_ctx_switch(&co->ctx, &S.main_ctx);
#else
    swapcontext(&co->ctx, &S.main_ctx);
#endif
}

void co_await(Coroutine *other) {
    assert(other && "co_await on NULL");
    if (other->state == CO_DONE) return;

    Coroutine *co = S.running;
    assert(co && "co_await called outside a coroutine");
    co->state    = CO_WAITING;
    co->awaiting = other;
#if USE_ASM_CTX
    co_ctx_switch(&co->ctx, &S.main_ctx);
#else
    swapcontext(&co->ctx, &S.main_ctx);
#endif
}

int co_id(void) {
    return S.running ? S.running->id : 0;
}

CoState co_state(const Coroutine *co) {
    assert(co);
    return co->state;
}

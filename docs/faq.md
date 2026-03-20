# Frequently Asked Questions

## General

### What is c-coroutine?

c-coroutine is a lightweight cooperative coroutine (fiber) library written in C. It provides a simple API to create, schedule, and switch between multiple execution contexts within a single OS thread — no kernel involvement, no threads, no locks.

### How is this different from threads?

Threads are preemptively scheduled by the OS and require synchronization primitives (mutexes, semaphores) to share data safely. Coroutines are cooperatively scheduled — a coroutine runs until it explicitly calls `co_yield()` or `co_await()`. This makes data sharing trivial and context switches much cheaper (no kernel trap, no TLB flush).

### Is c-coroutine production-ready?

It is a learning/hobby project with a known set of limitations (single-threaded, no signal safety, no dynamic stack growth). Review the [Known Limitations](design.md#known-limitations) section before using it in any critical path.

---

## Building & Installation

### What platforms are supported?

c-coroutine targets **x86-64 Linux** using the System V AMD64 ABI. A `ucontext`-based fallback is compiled in automatically on other POSIX platforms, so it should build on macOS and other Linux architectures, but this path receives less testing.

### How do I build the library?

```bash
make          # builds lib/libcoroutine.a
make tests    # builds and runs the test suite
make examples # builds the example programs
```

### What are the build dependencies?

Only a C11-compatible compiler (`gcc` or `clang`) and GNU `make`. No third-party libraries are required.

---

## API & Usage

### What is the minimum stack size?

The practical minimum is **4 096 bytes** (one page). Passing a smaller value to `co_create()` results in undefined behaviour. For most workloads 64 KB – 256 KB is a safe default.

### Can I pass multiple arguments to a coroutine?

`co_create()` accepts a single `void *arg`. Pack multiple values into a struct and pass a pointer to it:

```c
typedef struct { int x; int y; } Args;
Args a = {1, 2};
Coroutine *co = co_create(65536, my_func, &a);
```

### What happens when a coroutine returns?

Returning from the entry function marks the coroutine as finished. Any coroutine blocked in `co_await(co)` on it will be re-queued. The coroutine struct is **not** automatically freed — call `co_destroy()` when you are done with it.

### Can coroutines be reused after they finish?

No. Once a coroutine returns from its entry function it cannot be re-spawned. Create a new coroutine instead.

---

## Troubleshooting

### My program segfaults inside a coroutine.

The most common cause is a stack overflow. Increase the `stack_size` passed to `co_create()`. You can instrument the stack with a canary pattern to detect overflows during testing.

### The CI badge shows "failing" — is the library broken?

Check the [Actions tab](https://github.com/Harsh7115/c-coroutine/actions) for the latest run. A red badge on the README may lag one commit behind if GitHub Pages has not refreshed yet.

### I get a linker error: `undefined reference to co_yield`.

Make sure you link against `lib/libcoroutine.a` and include `include/coroutine.h`. The correct linker flag is `-Llib -lcoroutine`.

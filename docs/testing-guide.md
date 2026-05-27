# Testing Guide

This guide explains how to build and run the c-coroutine test suite, write new
tests, and integrate the suite into CI pipelines.

---

## 1. Prerequisites

| Tool | Minimum version | Notes |
|------|-----------------|-------|
| GCC or Clang | GCC 9 / Clang 11 | C11 support required |
| GNU Make | 4.x | `make -j` for parallel builds |
| Valgrind *(optional)* | 3.18 | Memory error detection |
| AddressSanitizer | bundled with GCC/Clang | Preferred over Valgrind in CI |

---

## 2. Building the Tests

```sh
# Debug build with AddressSanitizer (recommended for development)
make test CFLAGS="-g -fsanitize=address,undefined"

# Release build (no sanitizers, optimised)
make test CFLAGS="-O2 -DNDEBUG"

# Single test file
cc -g -I include tests/test_basic.c src/coroutine.c -o test_basic && ./test_basic
```

All test binaries are placed in `build/tests/`.

---

## 3. Running the Suite

```sh
# Run every test
make check

# Run with verbose output (prints each test name)
make check V=1

# Run under Valgrind
make check VALGRIND=1
```

Exit codes follow the TAP convention used by `make check`:

| Code | Meaning |
|------|---------|
| 0 | All tests passed |
| 1 | One or more tests failed |
| 77 | Test skipped (platform not supported) |
| 99 | Hard error (test binary crashed) |

---

## 4. Test Layout

```
tests/
├── test_basic.c          # co_create / co_resume / co_yield round-trip
├── test_cancel.c         # co_cancel and forced-unwind semantics
├── test_channel.c        # bounded SPSC channel correctness
├── test_scheduler.c      # round-robin and priority scheduler
├── test_stack_growth.c   # guard-page and overflow detection
├── test_stress.c         # long-running randomised stress test
└── helpers/
    ├── tap.h             # Tiny TAP producer (ok/not-ok macros)
    └── mock_clock.h      # Deterministic time source for timer tests
```

---

## 5. Writing a New Test

Every test file follows the same structure:

```c
#include "helpers/tap.h"
#include "coroutine.h"

/* --- helpers -------------------------------------------------------- */
static void simple_fn(void *arg) {
    int *counter = (int *)arg;
    (*counter)++;
    co_yield();
    (*counter)++;
}

/* --- test cases ----------------------------------------------------- */
static void test_resume_twice(void) {
    int n = 0;
    coro_t c = co_create(simple_fn, &n);
    ok(c != NULL, "co_create returns non-NULL");

    co_resume(c);
    ok(n == 1, "counter incremented after first resume");

    co_resume(c);
    ok(n == 2, "counter incremented after second resume");
    ok(co_done(c), "coroutine finished after body returns");

    co_destroy(c);
    ok(1, "co_destroy does not crash");
}

/* --- entry point ---------------------------------------------------- */
int main(void) {
    plan(5);          /* declare total number of ok() calls */
    test_resume_twice();
    return tap_finish();
}
```

### Naming conventions

- File: `tests/test_<feature>.c`
- Case function: `test_<what_is_verified>(void)`
- Use `ok(expr, "description")` for assertions; `skip(n, "reason")` to skip
  blocks that require unavailable hardware.

### Testing error paths

Wrap the call under test in a child process using `fork()` + `waitpid()` when
you need to verify that an assertion fires or a fatal error is handled:

```c
pid_t pid = fork();
if (pid == 0) {
    /* child: trigger the expected abort */
    co_resume(NULL);   /* should abort */
    _exit(0);
}
int status;
waitpid(pid, &status, 0);
ok(WIFSIGNALED(status) || WEXITSTATUS(status) != 0,
   "co_resume(NULL) terminates the process");
```

---

## 6. Sanitizer Flags Reference

```sh
# AddressSanitizer + UBSan (catch memory errors and UB)
CFLAGS="-g -fsanitize=address,undefined -fno-omit-frame-pointer"

# ThreadSanitizer (for multi-threaded scheduler tests)
CFLAGS="-g -fsanitize=thread"

# MemorySanitizer — Clang only
CFLAGS="-g -fsanitize=memory -fsanitize-memory-track-origins"
```

> **Note:** Do not mix ASan and TSan in the same binary; they conflict.

---

## 7. CI Integration

The repository ships a GitHub Actions workflow at
`.github/workflows/ci.yml` that runs the test matrix on every push and
pull request.  To reproduce it locally:

```sh
# Matrix leg: GCC + ASan
CC=gcc  make check CFLAGS="-g -fsanitize=address,undefined"

# Matrix leg: Clang + ASan
CC=clang make check CFLAGS="-g -fsanitize=address,undefined"

# Matrix leg: release
CC=gcc  make check CFLAGS="-O2 -DNDEBUG"
```

A failing CI run exits non-zero; inspect the `build/tests/*.log` artefacts
for full TAP output.

---

## 8. Coverage Reports

```sh
# Build with coverage instrumentation
make clean
make test CC=gcc CFLAGS="-g --coverage"
make check

# Generate HTML report
gcov src/*.c
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage-html
open coverage-html/index.html
```

Aim for **≥ 85 % line coverage** on `src/` before submitting a pull request.

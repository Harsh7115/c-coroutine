# Contributing to c-coroutine

Thank you for your interest in contributing! This document outlines how to get started, the coding standards, and the process for submitting changes.

## Getting Started

```bash
git clone https://github.com/Harsh7115/c-coroutine.git
cd c-coroutine
make          # build library + tests + examples
make test     # run the full test suite
```

Requirements: GCC or Clang, GNU Make, x86-64 Linux (or macOS for the ucontext fallback).

## Project Layout

```
include/   public API header (coroutine.h)
src/       library implementation (coroutine.c, asm_ctx.S)
tests/     unit / stress tests
examples/  usage demos
benchmarks/  micro-benchmarks (optional, not built by default)
```

## Coding Style

- **C standard**: C11 (`-std=c11`)
- **Indentation**: 4 spaces, no tabs
- **Naming**: `snake_case` for everything; public symbols prefixed with `co_`
- **Comments**: Doxygen-style `/** ... */` on public API; inline `//` for implementation notes
- **Error handling**: functions that can fail return `NULL` or `-1`; always check return values in tests

Run `make lint` before submitting — it invokes `cppcheck` with the same flags used in CI.

## Adding a Test

1. Create `tests/test_<feature>.c`.
2. Include `<assert.h>` and `"coroutine.h"`.
3. Write a `main()` that returns `0` on success and non-zero on failure.
4. Add an entry to the `TESTS` variable in `Makefile`.

## Submitting a Pull Request

1. Fork the repo and create a feature branch: `git checkout -b feat/my-feature`
2. Make your changes with clear, focused commits.
3. Ensure `make test` passes and `make lint` is clean.
4. Open a PR against `main` with a concise description of *what* and *why*.
5. Respond to review comments — we aim for a first review within 48 hours.

## Reporting Bugs

Open a GitHub Issue with:
- OS and compiler version (`gcc --version`)
- Minimal reproducer
- Expected vs. actual behaviour

## License

By contributing you agree that your changes will be released under the project's [MIT License](LICENSE).

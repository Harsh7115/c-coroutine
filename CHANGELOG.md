# Changelog

All notable changes to **c-coroutine** are documented here.
This project follows [Semantic Versioning](https://semver.org/).
Dates are in YYYY-MM-DD format.

---

## [Unreleased]

### Added
- `CHANGELOG.md` — this file
- `SECURITY.md` — vulnerability reporting policy
- `CONTRIBUTING.md` — contributor guide
- `.editorconfig` — editor configuration for consistent style
- `docs/design.md` — architecture deep-dive (context switch, scheduler, co_await internals)
- `docs/api_reference.md` — full public API reference
- `docs/faq.md` — frequently asked questions
- `benchmarks/bench_yield.c` — `co_yield` round-trip latency micro-benchmark
- `examples/producer_consumer.c` — bounded-buffer producer/consumer demo
- `.github/ISSUE_TEMPLATE/bug_report.md` — structured bug report template
- `.github/ISSUE_TEMPLATE/feature_request.md` — feature request template

### Fixed
- Removed `static` from `co_entry()` to restore external linkage required by
  `asm_ctx.S` (`co_trampoline` calls it across translation units).
- Added `--suppress=unusedStructMember:src/coroutine.c` to the CI cppcheck
  invocation; the `AsmCtx` struct fields are only accessed from assembly.

---

## [1.0.0] — 2026-03-17

### Added
- Initial public release.
- Hand-written x86-64 context switch (`src/asm_ctx.S`) following the
  System V AMD64 ABI (saves/restores rbp, rbx, r12–r15, rsp).
- `ucontext`-based fallback for non-x86-64 POSIX targets.
- Core API: `co_create`, `co_destroy`, `co_spawn`, `co_run`, `co_yield`, `co_await`.
- FIFO cooperative scheduler.
- Test suite: `test_basic`, `test_pipeline`, `test_stress`.
- Example programs: `generator`, `pipeline` (see `examples/`).
- GitHub Actions CI: build matrix on `ubuntu-latest` and `ubuntu-22.04`,
  plus `cppcheck` lint job.
- `README.md` with quick-start, API overview, and design notes.

---

[Unreleased]: https://github.com/Harsh7115/c-coroutine/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Harsh7115/c-coroutine/releases/tag/v1.0.0

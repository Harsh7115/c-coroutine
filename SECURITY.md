# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.x     | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

If you discover a security vulnerability in c-coroutine, please **do not** open a public GitHub issue.

Instead, report it privately via one of these channels:

- **GitHub Security Advisories**: Navigate to the [Security](https://github.com/Harsh7115/c-coroutine/security) tab and click "Report a vulnerability".
- **Email**: harshitjain30032004@gmail.com

Please include the following in your report:

- A description of the vulnerability and its potential impact
- Steps to reproduce the issue (proof-of-concept code if applicable)
- The version(s) of c-coroutine affected
- Any suggested mitigations or fixes

## Response Timeline

| Milestone            | Target      |
| -------------------- | ----------- |
| Initial response     | 48 hours    |
| Triage & assessment  | 5 days      |
| Patch release        | 14 days     |

## Scope

This policy covers the core library source code (`src/`, `include/`), the build system (`Makefile`), and the CI pipeline (`.github/`).

Out of scope: third-party dependencies, example programs, and benchmark code.

## Disclosure Policy

We follow a **coordinated disclosure** model. After a fix is released, we will publish a GitHub Security Advisory crediting the reporter (unless they prefer anonymity).

Thank you for helping keep c-coroutine secure!

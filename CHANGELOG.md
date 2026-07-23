# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `docs/v1_release_plan.md` — audited v1.0 release plan: defect inventory (C1–C24), workstream table, phased milestones, quality gates.
- Project rules: `CLAUDE.md` and `.cursor/rules` (graphify-first codebase navigation, project conventions).
- Regression tests for C4, C16, and C17.

### Fixed

- C4: `MemoryPool::isEmpty()`/`isFull()` bodies were swapped.
- C10: SIGPIPE protection (`MSG_NOSIGNAL`) on TCP/UDP sends — peer teardown no longer kills the process.
- C16: `optional<>` move semantics now match `std::optional` (moved-from source stays engaged).
- C17: UUID v4 version bits applied to the correct 64-bit half — generated strings are RFC-4122-compliant.
- C23: `nextPowerOf2` `value >> 32` undefined behavior on 32-bit `size_t` removed (all 3 copies).

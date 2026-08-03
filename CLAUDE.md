# AxonVex — Project Rules (Claude Code / AI agents)

AxonVex is a C++14 real-time framework: deterministic scheduling, typed processing pipelines, safety envelope, protocol adapters (ROS 2, MAVLink), plus a planned WebSocket gateway + Angular 17+ dashboard and (v1.1) a Visual System Composer.

**Canonical documents — read before non-trivial work:**
- `docs/v1_release_plan.md` — THE plan. Defect inventory **C1–C24** (§3, with file:line), architecture debt (§5), full-vision workstreams (§6), phases 0–10 (§8), quality gates (§9), composer foundations (§11). Reference defect IDs (e.g. "C3") in commits, PRs, and discussions.
- `docs/software_requirements_specification.md`, `docs/architecture_and_design.md`, `docs/project_backlog.md`, `docs/STYLE_GUIDE.md`.

---

## Build & Test (canonical commands)

```bash
conan install . --output-folder=build --build=missing            # deps (Conan 2)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_EXAMPLES=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure                       # all tests, single test_core binary

# Sanitizers — one at a time, in their own build dir (flags apply to every target)
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DAXONVEX_SANITIZER=address   # or thread, undefined
ctest --test-dir build-asan --output-on-failure
setarch $(uname -m) -R ctest --test-dir build-tsan               # TSan needs ASLR off on kernel 6.6+
```

Rules:
- **Never claim a change works without a successful build AND full ctest run.** Paste the failing output if it fails; never paper over.
- **Zero-warning policy.** The tree currently builds with 0 warnings; keep it that way. New warnings = broken build.
- Run sanitizer configs when they exist (Phase 1 adds ASan/UBSan/TSan presets); TSan is mandatory for any change touching threads, atomics, locks, or callbacks.
- Tests: GoogleTest under `tests/`, discovered by CTest. New test files follow `tests/<area>/<name>Test.cpp` and register in the root CMake test target.
- No sleep-based test synchronization. Use condition variables / polling-with-timeout helpers. Sleeps are the project's #1 flake source.

## Language & Toolchain constraints

- **C++14 strictly.** No C++17-isms (`std::optional`, `std::filesystem`, structured bindings, `if constexpr`, inline variables). Use `axonvex::utils::optional` and `axonvex_fs` (`detail/filesystem_compat.hpp`). Revisit only as an explicit, plan-level decision.
- Filesystem: `std::experimental::filesystem` via the compat alias; Linux GCC/non-Apple Clang links `stdc++fs` (handled in `axonvex_core/CMakeLists.txt`).
- Dependencies: managed by Conan only (nlohmann_json, GTest, oneTBB; later Boost.Beast, OpenSSL, benchmark). **Never vendor code or add a dependency for something a few lines can do.** New deps require a note in the PR justifying against the plan.
- Platform code: `#ifdef` islands must be explicit. **A platform that can't do the operation returns an error or fails to compile — never silently no-ops and returns success** (this bug class is being removed; do not add it back).

## Architecture rules (non-negotiable)

1. **Layering:** `axonvex_core` depends on nothing internal. All other libs (`interfaces`, `adapters`, `plugins`, `safety`, `io`, `visualization`, ROS2 plugin) depend only on `axonvex_core` (+ external SDKs). Core headers must not name upper-layer types (the old `SafetyManager` forward-decl in `system.hpp` was fixed via the core-owned `core::SafetyHook` interface — follow that pattern, don't reintroduce upper-layer names in core).
2. **RT-path invariant:** on scheduler/port hot paths — **no locks, no heap allocation, no string building, no I/O, no user callbacks**. Defer everything to the event thread. Any exception needs a comment naming the ceiling and a benchmark showing the cost.
3. **Never invoke user callbacks while holding a lock.** Snapshot under the lock, release, then dispatch. This exact bug exists in 4+ places (C12, C18) — do not add a fifth.
4. **No god classes.** New system-level responsibilities go into collaborators (`LifecycleController`, `UnitRegistry`, `SystemPortRegistry`, `EventBus`, `HealthMonitor` post-Phase-2), never into `AxonVexSystem` directly.
5. **No declared-but-unimplemented public API.** If you declare it in a header, implement and test it in the same PR, or don't declare it (C6 cleanup).
6. **Ownership is honest.** `unique_ptr` for owned, documented stable-reference contract for non-owned raw pointers. Types holding mutexes are non-movable — delete the move ops, don't `=default` them.
7. **Composer foundations are load-bearing** (plan §11): `SystemSpec`, `UnitFactory`, unit introspection metadata, `loadFromSpec()`, gateway `spec/*` API. Treat them as core features with tests, never optional extras. Every new ProcessingUnit type must register with the factory and describe its ports/parameters.
8. **Lock-free claims require proof.** Anything labeled lock-free needs a TSan-clean stress test and a comment explaining the memory-ordering argument (see `ThreadSafeQueue` as the good example; `MemoryPool` C3/ABA as the cautionary one).

## Style

- `.clang-format` at root is law — run it on touched files before committing.
- Naming: files `camelCase.cpp/.hpp` (e.g. `timingController.cpp`); classes `PascalCase`; methods/variables `camelCase`; members trailing underscore (`stats_`); namespaces `axonvex::core|interfaces|adapters|safety|plugins`.
- Headers: keep implementation out of headers unless it's a template. Big inline headers (logger.hpp, ports.hpp) are debt being reduced — don't grow them.
- Comments state constraints the code can't show (memory ordering, RT budgets, protocol quirks) — not narration. Deliberate shortcuts name their ceiling and upgrade path.
- Match `docs/STYLE_GUIDE.md` on anything it covers; it wins over this list on conflict.

## Workflow

1. **Bugs:** reproduce as a failing test first (regression test per fix), fix root cause at the shared function — not the symptom at one caller. Reference the plan defect ID (C1–C24) in the commit message when applicable.
2. **Features:** TDD where feasible — write the test against the intended API first. Every feature PR includes tests and doc updates in the same PR.
3. **Branches:** work off `feature/*` branches; `main` is protected by CI. Commit/push only when asked (agents).
4. **Pre-commit checklist:** build clean → full ctest green → clang-format on touched files → `graphify update .` → docs synced (below) → CHANGELOG entry for user-visible changes.
5. **Phase discipline:** work follows `docs/v1_release_plan.md` §8 ordering. Don't start Phase-N work while Phase-(N−1) exit criteria are red, unless explicitly told.

## Docs-syncing (every PR that changes behavior)

- **README claims rule:** every claim in README maps to code, a benchmark, or a tracked v1.0 workstream with an honest status. Never add a capability claim without shipping or tracking it. Update "Implementation Status" when a feature lands.
- Keep `docs/v1_release_plan.md` current: mark defect IDs fixed, tick phase exit criteria.
- Public API changes → update SRS requirement status + `docs/architecture_and_design.md` diagrams if structure moved.
- New/changed benchmarks → `docs/benchmarks.md` (Phase 3+) is the only place performance numbers live; README links, never inlines, numbers.
- `graphify update .` after code changes (AST-only, free) so the knowledge graph stays truthful.
- CHANGELOG.md: Keep-a-Changelog format, entry per user-visible change.

---

## Tooling roster — skills / agents / MCPs and exactly when to use them

**C++ core work (Phases 1–7):**
- `ecc:cpp-reviewer` agent (via `/ecc:cpp-review`) — MANDATORY after any C++ change: memory safety, concurrency, idioms. Run before requesting human review.
- `ecc:cpp-build-resolver` agent (via `/ecc:cpp-build`) — any CMake/compiler/linker failure. Minimal-diff fixes only; no architecture edits from build fixing.
- `/ecc:cpp-test` + `superpowers:test-driven-development` — writing tests first for features/bugfixes; GoogleTest patterns.
- `superpowers:systematic-debugging` — invoke BEFORE proposing any bug fix; prevents symptom-patching.
- `superpowers:verification-before-completion` — before claiming done / opening a PR: run the checks, show output.
- `ecc:silent-failure-hunter` agent — after touching transports, platform `#ifdef` fallbacks, or error paths (this codebase's signature bug class).
- `ecc:code-simplifier` agent — executing the plan §7 deletion list and post-fix cleanup; behavior-preserving only.
- `ecc:performance-optimizer` agent — Phase 5 (WS-PERF) hot-path work; pair with Google Benchmark results, never optimize blind.

**Security (Phase 8, plus anytime auth/parsing/untrusted input is touched):**
- `ecc:security-reviewer` agent — gateway endpoints, TLS setup, TOTP/MFA, RBAC checks, plugin dlopen path, `Path` validation, any deserialization. Run on every WS-SEC PR.
- `/ecc:security-scan` — periodic sweep of the agent/hook/config surface.

**Dashboard & gateway (Phase 9 / v1.1 composer):**
- `ecc:angular-developer` skill — Angular 17+ standalone/signals conventions for `dashboard/`.
- `ecc:typescript-reviewer` agent — every TS change, same mandate as cpp-reviewer for C++.
- Playwright MCP (`mcp__plugin_playwright_*`) / `ecc:e2e-runner` agent — dashboard e2e journeys (login+MFA → charts → replay → e-stop). Chrome DevTools MCP (`mcp__plugin_ecc_chrome-devtools__*`) for rendering/perf/console debugging of the running dashboard.
- Postman MCP + `postman:generate-spec` / `postman:run-collection` — the gateway REST/WS control API (`spec/validate|get|deploy`, auth endpoints): generate the OpenAPI spec from code, keep a collection as the API contract test, run it in CI.

**Docs & research:**
- `ecc:docs-lookup` agent (Context7 MCP) — current, version-correct docs for Boost.Beast, OpenSSL, rclcpp, MAVLink, Angular, Conan/CMake APIs. Use instead of trusting training-data recall for any external API signature.
- `ecc:doc-updater` agent / `/ecc:update-docs` / `/ecc:update-codemaps` — after each phase milestone to resync READMEs/codemaps from source of truth.
- `deep-research` skill — one-off technology decisions (e.g. canvas library spike for the composer: Rete.js vs JointJS vs custom).

**Codebase navigation & memory:**
- graphify (rules below) — first stop for "how does X relate to Y" questions.
- mempalace / Claude memory — record decisions with plan references (defect IDs, phase numbers) so future sessions inherit context, not vibes.

**Recommended project tooling (not agents — add to repo):** `.clang-tidy` config (CI already invokes clang-tidy with no config — currently a no-op gate), `compile_commands.json` export for clangd, Google Benchmark (Phase 3), cppcheck/include-what-you-use as optional local linting.

**When NOT to reach for tooling:** trivial one-line fixes don't need the full agent pipeline — build + ctest + clang-format still apply, everything else scales with risk. Never add a new MCP/plugin dependency to solve something an existing one or a shell command already covers.

---

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).

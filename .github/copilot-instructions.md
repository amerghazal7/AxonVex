# AxonVex — Copilot Instructions

## Project Overview

AxonVex is a C++17 real-time processing framework built around a modular library architecture. The system design, requirements, and roadmap live in `docs/` — always consult them before making architectural decisions.

| Document | Purpose |
|----------|---------|
| `docs/architecture_and_design.md` | Package model, layer rules, anti-patterns |
| `docs/software_requirements_specification.md` | Current and planned requirements |
| `docs/implementation_plan.md` | 5-phase roadmap |
| `docs/STYLE_GUIDE.md` | Naming, formatting, file layout |
| `docs/project_backlog.md` | Task tracking |

## Build System

CMake 3.20+, Conan for dependencies. Six libraries under `src/libs/`:

| Library | Type | Role |
|---------|------|------|
| `axonvex_core` | SHARED | Runtime, orchestration, ports, timing |
| `axonvex_interfaces` | INTERFACE | Protocol abstractions |
| `axonvex_plugins` | INTERFACE | Plugin API |
| `axonvex_safety` | INTERFACE | Safety abstractions |
| `axonvex_io` | INTERFACE | I/O abstractions |
| `axonvex_visualization` | INTERFACE | Visualization abstractions |

All interface libraries depend only on `axonvex_core`. Exported under the `axonvex::` CMake namespace.

### Build commands

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_EXAMPLES=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

**Always build and run tests after every code change.** Use the workspace tasks "Build All" then "Run Tests", or the commands above.

### Adding source files

Source files are listed explicitly in each library's `CMakeLists.txt` — never use `GLOB` or `GLOB_RECURSE`. When adding a new `.cpp` file, add it to the `target_sources()` call in the relevant `src/libs/<library>/CMakeLists.txt`.

### Adding a new library

Follow the pattern in `src/libs/axonvex_core/CMakeLists.txt`. Add the subdirectory in `src/libs/CMakeLists.txt`. Register the library in the root `CMakeLists.txt` install section. Core must not depend on higher-level libraries.

### Conan dependencies

Defined in `conanfile.txt`: nlohmann_json, gtest, onetbb. To add a dependency, update `conanfile.txt` and the relevant library's `CMakeLists.txt` find/link calls.

## Testing — TDD Workflow

**Test-Driven Development is mandatory.** For every new feature or bug fix:

1. Write a failing test first in `tests/<module>/`.
2. Implement the minimum code to make it pass.
3. Refactor while keeping tests green.
4. Build and run the full suite before considering the work done.

### Test structure

- Single binary: `test_core`, linked to all libraries + `GTest::GTest` + `GTest::Main`.
- Tests are registered in the root `CMakeLists.txt` under `if(BUILD_TESTING)`.
- `gtest_discover_tests()` auto-discovers individual tests for CTest.
- Test files go in `tests/<module>/` matching the source module (core, types, utils, io, plugins, safety, interfaces).

### Adding a test file

1. Create the file in `tests/<module>/<name>Test.cpp`.
2. Add it to the `target_sources(test_core ...)` block in the root `CMakeLists.txt`.
3. Do **not** add a `main()` function — `GTest::Main` provides it.

### Critical: avoid ODR violations

All test helper classes (mocks, stubs, test fixtures) must be in an **anonymous namespace** or a **uniquely-named namespace** within each `.cpp` file. Multiple test files are compiled into one binary; duplicate class names at the same scope cause silent, hard-to-debug failures (wrong vtable picked by linker).

```cpp
// CORRECT — anonymous namespace prevents ODR collisions
namespace {
class MockProcessingUnit : public ProcessingUnit { ... };
} // anonymous namespace
```

### Performance/timing tests

Use generous thresholds (5x headroom minimum). These tests run on varied hardware and CI runners. Prefer assertions on behavior, not absolute timing.

## Code Style

Follow `docs/STYLE_GUIDE.md`. Key rules:

- **Files**: `camelCase.hpp` / `camelCase.cpp`
- **Classes**: `CamelCase` — **Types**: `CamelCase`
- **Functions/methods**: `camelCase()`
- **Member variables**: `snake_case_` (trailing underscore)
- **Constants**: `UPPER_CASE`
- **Namespaces**: `snake_case` — e.g., `axonvex::core`
- **Indentation**: 4 spaces, no tabs
- **Line length**: 100 characters max
- **Braces**: K&R style, always use braces even for single-statement blocks
- **Headers**: `#pragma once`, never include guards
- **Include order**: own header → system → third-party → project headers

Use RAII, smart pointers (`std::unique_ptr` / `std::shared_ptr`), and `std::optional`. Avoid raw `new`/`delete`.

## Architecture Rules

These are hard constraints from the system design — do not violate them:

1. **Core independence**: `axonvex_core` must not depend on any interface, plugin, safety, IO, or visualization library.
2. **Adapter boundary**: External formats (ROS, MAVLink, PX4) must be translated at adapter boundaries, never leak into core.
3. **Mission testability**: Mission logic must be executable with mock adapters — never couple to live transport in core paths.
4. **Safety separation**: Safety checks belong in the safety framework layer, not scattered as ad-hoc application logic.
5. **No singletons**: Avoid singleton patterns for system-wide control. Use dependency injection.
6. **No global state**: Configuration, logging, and system state flow through explicit objects, not globals.

## Include Paths

Each library exposes headers under its own prefix:

```cpp
#include <axonvex_core/system.hpp>
#include <axonvex_core/processingUnit.hpp>
#include <axonvex_interfaces/protocolInterface.hpp>
#include <axonvex_plugins/pluginInterface.hpp>
```

Never use bare `#include "system.hpp"` — always use the library-prefixed angle-bracket form.

## Documentation

When making changes that affect architecture, public API, or module boundaries:

1. Update `docs/architecture_and_design.md` if the component map changes.
2. Update `docs/software_requirements_specification.md` if requirements are fulfilled or added.
3. Update `docs/implementation_plan.md` if a phase milestone is completed.
4. Use Doxygen-style comments (`@brief`, `@param`, `@return`) on all public API surfaces.

## Validation Checklist

Before considering any task complete:

- [ ] Code compiles with no warnings: `cmake --build build -j8`
- [ ] All tests pass: `ctest --test-dir build --output-on-failure`
- [ ] New code has corresponding tests
- [ ] No ODR violations (anonymous namespaces for test helpers)
- [ ] Style guide followed (naming, formatting, include order)
- [ ] Relevant docs updated if architecture/API changed
- [ ] Changes align with `docs/architecture_and_design.md` layer rules

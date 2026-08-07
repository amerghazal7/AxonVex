# AxonVex — Copilot Instructions

## Project Overview

AxonVex is a **C++14** real-time processing framework built around a modular library architecture. The system design, requirements, and roadmap live in `docs/` — always consult them before making architectural decisions.

**Language baseline:** `CMAKE_CXX_STANDARD` is **14** in the root `CMakeLists.txt`. Do not introduce C++17-only features (e.g. `std::optional`, `std::filesystem`, `if constexpr`, structured bindings, inline variables for ODR-used `static constexpr` data members) without an explicit standard bump. Path/file code uses `std::experimental::filesystem` via `axonvex_core/detail/filesystem_compat.hpp`; on Linux/GNU and non-Apple Clang, `axonvex_core` links **`stdc++fs`**.

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
| `axonvex_safety` | INTERFACE | Safety abstractions (Watchdog, SafetyPolicy, SafetyManager) |
| `axonvex_io` | INTERFACE | I/O abstractions |
| `axonvex_visualization` | INTERFACE | Visualization abstractions + WebSocket gateway |
| `axonvex_adapters` | INTERFACE | Adapter contract (AdapterInterface, AdapterBase, MockAdapter) |
| `axonvex_ros2` | INTERFACE | ROS 2 plugin (ROS2Adapter, type caster registry) — optional |

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

Defined in `conanfile.py`: nlohmann_json, gtest, onetbb, benchmark (test_requires). To add a dependency, update `conanfile.py` and the relevant library's `CMakeLists.txt` find/link calls.

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

## Visualization Stack (Angular 17+)

The web visualization layer is a separate build artifact from the C++ framework:

- **Dashboard**: Angular 17+ project using standalone components, signals, and new control flow (`@if`, `@for`).
- **Transport**: WebSocket service with auto-reconnect, per-channel subscription API, and binary/JSON negotiation.
- **Charts**: ngx-charts or D3.js wrappers for real-time telemetry.
- **3D**: Three.js integrated via Angular component for spatial data (poses, point clouds, trajectories).
- **Layout**: Configurable drag-and-drop panel grid with session persistence.
- **Theming**: Light/dark mode, responsive for desktop and tablet.

The C++ side provides a WebSocket gateway that bridges `TelemetryBus` channels and framework state to web clients. The Angular dashboard connects exclusively through this gateway — never link to C++ internals.

When working on visualization code:
- Follow Angular style guide and use strict TypeScript.
- Prefer signals over BehaviorSubject for new state.
- Keep WebSocket message schemas documented in `docs/` or co-located `*.schema.json` files.
- Dashboard must be buildable and servable independently (`ng serve` / `ng build`).

## Architecture Rules

These are hard constraints from the system design — do not violate them:

1. **Core independence**: `axonvex_core` must not depend on any interface, plugin, safety, IO, or visualization library.
2. **Adapter boundary**: External formats (ROS, MAVLink, PX4) must be translated at adapter boundaries, never leak into core.
3. **Mission testability**: Mission logic must be executable with mock adapters — never couple to live transport in core paths.
4. **Safety separation**: Safety checks belong in the safety framework layer, not scattered as ad-hoc application logic.
5. **No singletons**: Avoid singleton patterns for system-wide control. Use dependency injection.
6. **No global state**: Configuration, logging, and system state flow through explicit objects, not globals.
7. **Dashboard isolation**: The Angular dashboard communicates only through the WebSocket gateway — no direct C++ linkage or shared-memory shortcuts.

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

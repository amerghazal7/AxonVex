# Contributing to AxonVex

## Build and test

```bash
conan install . --output-folder=build --build=missing
cmake -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_EXAMPLES=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Rules

- **Zero warnings.** The build must stay warning-free; a change that introduces a warning is not done.
- **C++14 only.** No C++17-or-later features. Use the in-tree compat layers (`optional.hpp`, `filesystem_compat.hpp`) instead of `std::optional`/`std::filesystem`.
- **Formatting.** Run `clang-format` with the repo's `.clang-format`; follow [docs/STYLE_GUIDE.md](docs/STYLE_GUIDE.md).
- **Tests first.** New features are developed TDD-style; every bug fix lands with a regression test that fails without the fix.
- **Defect IDs.** Fixes for audited defects reference their ID (C1–C24, defined in [docs/v1_release_plan.md §3](docs/v1_release_plan.md)) in the commit message, e.g. `fix(core): C4 swap MemoryPool isEmpty/isFull`.
- **Branches.** Branch off a `feature/*` branch; do not commit directly to `main`.

## PR checklist

1. Build is clean (zero warnings).
2. `ctest --test-dir build --output-on-failure` is green.
3. `clang-format` applied to touched files.
4. `graphify update .` run after code changes (keeps the knowledge graph current).
5. Docs affected by the change are updated (README, docs/, example READMEs).
6. `CHANGELOG.md` entry added under `[Unreleased]`.

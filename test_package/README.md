# test_package / downstream consumer

Proves an *installed* AxonVex package (headers + `axonvexConfig.cmake` +
shared libraries) is actually consumable via `find_package(axonvex)` —
built as a standalone project against the install tree, never
`add_subdirectory()`'d from the root build (that would exercise in-tree
targets and never catch the packaging bugs this exists to catch — see git
history on this directory for the two it already caught: the header
double-nesting bug and the missing per-component `_FOUND` flags in
`axonvexConfig.cmake.in`).

## Without conan (verified in this repo's environment)

```bash
# from the repo root
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
cmake --install build

cd test_package
cmake -B build -DCMAKE_PREFIX_PATH=<repo-root>/install/libs/lib/cmake/axonvex
cmake --build build
./build/downstream_consumer
```

## Via conan (NOT run in this environment — conan isn't installed here)

This directory doubles as the recipe's `test_package`, invoked automatically:

```bash
# from the repo root
conan create . --build=missing
```

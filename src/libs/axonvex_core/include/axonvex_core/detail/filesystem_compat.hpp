#pragma once

/**
 * Filesystem shim. The project targets C++14 strictly (see CLAUDE.md) and
 * std::experimental::filesystem is the intended C++14 vehicle for that:
 * preferred whenever a toolchain ships it (GCC libstdc++ links stdc++fs, see
 * axonvex_core/CMakeLists.txt).
 *
 * Apple Clang / Xcode's libc++ dropped <experimental/filesystem> entirely
 * (it was never TS-complete there), so this header falls back to the
 * standardized <filesystem> when the experimental one is unavailable. That
 * fallback only produces a working std::filesystem, though, if the
 * translation unit is actually compiled as C++17: verified locally that
 * libstdc++'s <filesystem> wraps its whole contents in
 * `#if __cplusplus >= 201703L` and compiles to an empty header otherwise —
 * `g++ -std=c++14 -fsyntax-only` on a TU that includes just <filesystem> and
 * names std::filesystem fails with "'filesystem' is not a namespace-name".
 * libc++ follows the same _LIBCPP_STD_VER-gating pattern for every
 * post-C++11 standard-library addition (std::optional, std::variant,
 * string_view, ...), so the same failure on Apple's libc++ under -std=c++14
 * is the reasoned expectation here, NOT something verified on a real Mac —
 * no Apple toolchain is available in this environment. To make the fallback
 * usable, the root CMakeLists.txt raises CMAKE_CXX_STANDARD to 17 only under
 * `if(APPLE)`, after project() (same ordering rule as the project-wide C++14
 * pin — a set() before project() would be clobbered by the Conan toolchain).
 * That is a narrow, vendor-forced, platform-scoped exception to "C++14
 * strictly": every other platform still compiles at C++14, and this
 * codebase's own sources must still avoid C++17-isms (nothing here
 * introduces any) — only the compiler's *accepted* dialect widens on Apple,
 * to let its only available filesystem header actually define its contents.
 *
 * API-compatibility note: every axonvex_fs call site in this codebase
 * (path.cpp, jsonSerializer.hpp, fileManager.hpp, processingUnit.hpp) uses
 * only the throwing overloads (path, exists, is_regular_file, is_directory,
 * file_size, last_write_time, *_directory_iterator, create_director{y,ies},
 * remove(_all), copy_file with copy_options, rename, absolute/canonical,
 * temp_directory_path, current_path, file_time_type) — none of the
 * error_code overloads or the handful of members that differ between the TS
 * and the standardized API are used, so both namespaces are drop-in
 * compatible for this codebase's surface (confirmed by grep, not assumed).
 */
#if defined(__has_include)
#if __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace axonvex_fs = std::experimental::filesystem;
#elif __has_include(<filesystem>)
#include <filesystem>
namespace axonvex_fs = std::filesystem;
#else
#error "axonvex_fs: neither <experimental/filesystem> nor <filesystem> is available"
#endif
#else
// No __has_include (pre-2017 compiler): fall back to the C++14-intended header directly.
#include <experimental/filesystem>
namespace axonvex_fs = std::experimental::filesystem;
#endif

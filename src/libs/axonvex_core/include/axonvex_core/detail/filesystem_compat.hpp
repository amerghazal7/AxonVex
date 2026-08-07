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
 *
 * Branch selection is platform-gated, not header-presence-gated, on Apple:
 * __has_include(<experimental/filesystem>) is NOT a safe test there. libc++
 * did not delete the header when it dropped the TS implementation — it
 * ships a stub whose entire body is `#error "<experimental/filesystem> has
 * been removed. Use <filesystem> instead."`. A presence check alone would
 * see the stub, take the experimental branch, and hard-error via the
 * stub's own #error — silently defeating this whole fallback. So on Apple
 * (__APPLE__) this header never probes/includes <experimental/filesystem>
 * at all; it goes straight to <filesystem>, gated on the compiled dialect
 * (root CMakeLists.txt raises CMAKE_CXX_STANDARD to 17 if(APPLE), and
 * axonvex_core's CMakeLists.txt exports that as a PUBLIC cxx_std_17
 * compile-feature requirement so it also reaches downstream consumers).
 * If that dialect requirement doesn't reach a given TU for any reason, the
 * #error below names the actual cause instead of failing later with an
 * opaque "'filesystem' is not a namespace-name".
 *
 * Every other platform (GNU libstdc++, non-Apple Clang) keeps the original
 * header-presence check unchanged: libstdc++ ships a real, non-poisoned
 * <experimental/filesystem> unconditionally regardless of -std=, and this
 * codebase links two TUs (the ROS 2 plugin/smoke test) at -std=gnu++17 for
 * unrelated (rclcpp) reasons while axonvex_core's own .so is still built at
 * C++14 — picking <filesystem> there instead would give class Path (which
 * stores an axonvex_fs::path member) two different, ABI-incompatible
 * definitions of axonvex_fs::path depending on which TU's dialect happened
 * to compile it, corrupting any Path object that crosses the .so boundary.
 * Selecting on dialect instead of presence is only safe on Apple because
 * the root CMakeLists.txt's if(APPLE) raises the *entire* build to C++17
 * uniformly, so every TU (including this one) resolves to the same
 * std::filesystem there — no such uniform guarantee exists on Linux.
 */
#if defined(__has_include)
#if defined(__APPLE__)
#if __cplusplus >= 201703L && __has_include(<filesystem>)
#include <filesystem>
namespace axonvex_fs = std::filesystem;
#else
#error                                                                                             \
    "axonvex_fs: on Apple, <experimental/filesystem> is a removed/poisoned header (AXONVEX_FS_NEEDS_CXX17_ON_APPLE) -- the <filesystem> fallback requires this TU to compile at C++17+; see the root CMakeLists.txt if(APPLE) block and axonvex_core's exported cxx_std_17 requirement."
#endif
#elif __has_include(<experimental/filesystem>)
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

// Downstream consumer smoke test: doubles as the conan `test_package` and as
// a standalone example of consuming an *installed* AxonVex package via
// find_package(axonvex) — see test_package/README.md for both usages.
//
// It must exercise a real cross-.so call, not just a header-only constant: a
// header compiling proves nothing about whether the installed headers,
// CMake package config, and linked .so actually agree with each other.
// axonvex::core::Logger looked like a good candidate but is actually a fully
// inline (header-defined) class — the linker's --as-needed silently drops
// libaxonvex_core.so.1 for a binary that only touches it, which would have
// let a real linkage break through this test undetected. Path's static
// methods are genuinely out-of-line (defined only in path.cpp), so a link
// failure here means the installed headers/library/SONAME actually
// disagree.
#include <axonvex_core/path.hpp>
#include <axonvex_core/version.hpp>
#include <cstdio>

int main() {
    std::printf("AxonVex downstream consumer built against v%s (SOVERSION major %d)\n",
                axonvex::core::kVersionString, axonvex::core::kVersionMajor);

    // Path::getDefaultAppDir()/toString() are defined only in path.cpp,
    // inside libaxonvex_core.so — resolving them here proves the installed
    // package's headers, axonvexConfig.cmake target, and shared library
    // agree.
    axonvex::core::Path appDir = axonvex::core::Path::getDefaultAppDir();
    std::printf("axonvex::core::Path::getDefaultAppDir() resolved via the installed package: %s\n",
                appDir.toString().c_str());
    return 0;
}

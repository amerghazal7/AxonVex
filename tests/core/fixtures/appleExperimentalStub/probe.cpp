// Probe source for cmake/filesystemCompatAppleDialectCheck.cmake. Not built
// by the normal target graph -- the check script invokes the compiler on
// this file directly (-fsyntax-only) under several -D/-std combinations to
// exercise filesystem_compat.hpp's preprocessor branch selection without
// needing a real Apple toolchain.
#include <axonvex_core/detail/filesystem_compat.hpp>

int main() {
    axonvex_fs::path p("x");
    (void)p;
    return 0;
}

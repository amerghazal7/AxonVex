#pragma once

/**
 * C++14 filesystem: use std::experimental::filesystem (link stdc++fs on GCC).
 * Public API exposes paths as axonvex_fs::path — do not use std::filesystem in
 * AxonVex code so the project stays C++14-clean.
 */
#include <experimental/filesystem>

namespace axonvex_fs = std::experimental::filesystem;

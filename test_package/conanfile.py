"""conan `test_package` for the axonvex recipe — run automatically by
`conan create ..` from the repo root. Builds and runs downstream_consumer.cpp
against the just-packaged axonvex, via the CMakeLists.txt in this directory
(the same CMakeLists.txt that also works standalone against a plain `cmake
--install` tree — see this directory's README.md).

Verified: `conan create .. --build=missing -s build_type=Release` from the
repo root runs this file, builds downstream_consumer against the packaged
axonvex, and executes it successfully (Conan 2.31.2 / gcc 11.4, default
profile). The plain-CMake path (this directory's README.md, against a
manual `cmake --install`) was also run and confirmed working.
"""
import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


class AxonVexTestPackageConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            bin_path = os.path.join(self.cpp.build.bindir, "downstream_consumer")
            self.run(bin_path, env="conanrun")

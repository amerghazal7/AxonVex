"""AxonVex Conan 2 recipe.

Supersedes the old conanfile.txt: `conan install .` still works exactly the
same for local dev builds (this recipe's requirements()/generate() produce
the same CMakeDeps/CMakeToolchain files conanfile.txt did), and `conan
create .` additionally builds+packages axonvex itself and runs
test_package/ against the result.

NOT VERIFIED IN THIS ENVIRONMENT: conan is not installed here (see the
worktree's task notes) — this recipe has been written against the
documented Conan 2 ConanFile/CMake/CMakeDeps/CMakeToolchain API but never
run through `conan install .` or `conan create .`. A maintainer with conan
available must run both before trusting this file; report any failure
verbatim rather than assuming it's close enough.
"""
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy


class AxonVexConan(ConanFile):
    name = "axonvex"
    # Keep in lockstep with the root CMakeLists.txt `project(... VERSION ...)`
    # call — that call, not this string, is version.hpp's source of truth;
    # this one just has to agree with it for the conan package version to be
    # meaningful.
    version = "1.0.0"
    license = "See repository LICENSE"
    description = "AxonVex: a C++14 real-time framework for deterministic scheduling, " \
                   "typed processing pipelines, and protocol adapters."
    url = "https://github.com/axonvex/axonvex"  # ponytail: placeholder until the repo has a public remote
    settings = "os", "compiler", "build_type", "arch"
    options = {"build_plugins": [True, False]}
    default_options = {"build_plugins": True}

    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "src/*",
        "plugins/*",
        "examples/*",
        "conanfile.txt",
    )

    def requirements(self):
        self.requires("nlohmann_json/3.11.2")
        self.requires("onetbb/2022.2.0")

    def build_requirements(self):
        # Only needed for BUILD_TESTING; conan_provide_dependency wouldn't be
        # asked for it on a package() build (test-package builds are separate,
        # non-BUILD_TESTING configures), but declaring it here keeps `conan
        # install .` at the repo root matching the old conanfile.txt exactly.
        self.test_requires("gtest/1.14.0")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["BUILD_TESTING"] = "OFF"
        tc.variables["BUILD_EXAMPLES"] = "OFF"
        tc.variables["BUILD_PLUGINS"] = "ON" if self.options.build_plugins else "OFF"
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE*", self.source_folder, self.package_folder)

    def package_info(self):
        # Mirrors the EXPORT_NAME set on each CMake target (set_target_-
        # properties(... PROPERTIES EXPORT_NAME <name>)) so a consumer's
        # target_link_libraries(foo axonvex::core) works identically whether
        # axonvex came from `find_package(axonvex)` against a plain cmake
        # --install tree or from conan's generated axonvexConfig.cmake.
        self.cpp_info.set_property("cmake_file_name", "axonvex")
        self.cpp_info.set_property("cmake_target_name", "axonvex::axonvex")

        core = self.cpp_info.components["core"]
        core.set_property("cmake_target_name", "axonvex::core")
        core.libs = ["axonvex_core"]
        core.requires = ["nlohmann_json::nlohmann_json", "onetbb::onetbb"]

        for name, requires in (
            ("interfaces", ["core"]),
            ("adapters", ["core"]),
            ("safety", ["core"]),
            ("io", ["core"]),
            ("visualization", ["core"]),
            ("plugins", ["core"]),
        ):
            comp = self.cpp_info.components[name]
            comp.set_property("cmake_target_name", "axonvex::{}".format(name))
            comp.requires = requires

        net = self.cpp_info.components["net"]
        net.set_property("cmake_target_name", "axonvex::net")
        net.libs = ["axonvex_net"]
        net.requires = ["core", "interfaces"]

        if self.options.build_plugins:
            ros2 = self.cpp_info.components["ros2"]
            ros2.set_property("cmake_target_name", "axonvex::ros2")
            ros2.libs = ["axonvex_ros2"]
            ros2.requires = ["core"]

"""AxonVex Conan 2 recipe.

Supersedes the old conanfile.txt: `conan install .` still works exactly the
same for local dev builds (this recipe's requirements()/generate() produce
the same CMakeDeps/CMakeToolchain files conanfile.txt did), and `conan
create .` additionally builds+packages axonvex itself and runs
test_package/ against the result.

Verified with Conan 2.31.2 / gcc 11.4 (default profile, Release): both
`conan install . --output-folder=<dir> --build=missing -s build_type=Release`
and `conan create . --build=missing -s build_type=Release` succeed —
including test_package's downstream_consumer, which builds and runs against
the just-packaged axonvex (COMPONENTS core, ros2 present because this
environment happens to have rclcpp). Not exercised here: other compilers/OS,
`build_plugins=False`, and any build_type besides Release/Debug — a
maintainer changing those should re-run both commands.
"""
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain
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
        # Google Benchmark backs the WS-PERF microbenchmarks (-DAXONVEX_BUILD_BENCHMARKS=ON,
        # off by default). Dev-only like gtest: nothing the library itself links against.
        self.test_requires("benchmark/1.8.3")

    # No layout()/cmake_layout(): this recipe's `conan install .
    # --output-folder=build` must drop conan_toolchain.cmake directly under
    # that output folder (matching README.md / CLAUDE.md / CI, which all
    # pass -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake). cmake_layout()
    # relocates it to build/<BuildType>/generators/conan_toolchain.cmake
    # instead, breaking every one of those documented/CI invocations.

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["BUILD_PLUGINS"] = "ON" if self.options.build_plugins else "OFF"
        tc.generate()

    def build(self):
        cmake = CMake(self)
        # BUILD_TESTING/BUILD_EXAMPLES are forced OFF only for this recipe's
        # own package build (a minimal `conan create .` package build has no
        # need for tests/examples) — NOT via generate()'s CMakeToolchain,
        # which is the same toolchain file local dev builds and CI configure
        # against; baking them there silently zeroed CI's test count (C48-
        # adjacent: a build_type/config mismatch isn't the only way a shared
        # toolchain variable silently defeats a caller's own -D flag).
        cmake.configure(variables={"BUILD_TESTING": "OFF", "BUILD_EXAMPLES": "OFF"})
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

        # install()'s per-component layout is axonvex_<lib>/libs (shared libs
        # only — interfaces/adapters/safety/io/visualization/plugins are
        # INTERFACE targets, header-only, no .libs) and axonvex_<lib>/include
        # (all components) — see src/libs/*/CMakeLists.txt and plugins/
        # axonvex_ros2/CMakeLists.txt install(TARGETS/DIRECTORY) calls. conan
        # defaults to lib/ and include/, which install() never populates, so
        # every component needs its libdirs/includedirs pointed at the real
        # location or a consumer's find_package(axonvex) links against
        # nothing.
        core = self.cpp_info.components["core"]
        core.set_property("cmake_target_name", "axonvex::core")
        core.libs = ["axonvex_core"]
        core.libdirs = ["axonvex_core/libs"]
        core.includedirs = ["axonvex_core/include"]
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
            comp.libdirs = []
            comp.includedirs = ["axonvex_{}/include".format(name)]
            comp.requires = requires

        net = self.cpp_info.components["net"]
        net.set_property("cmake_target_name", "axonvex::net")
        net.libs = ["axonvex_net"]
        net.libdirs = ["axonvex_net/libs"]
        net.includedirs = ["axonvex_net/include"]
        net.requires = ["core", "interfaces"]

        if self.options.build_plugins:
            ros2 = self.cpp_info.components["ros2"]
            ros2.set_property("cmake_target_name", "axonvex::ros2")
            ros2.libs = ["axonvex_ros2"]
            ros2.libdirs = ["axonvex_ros2/libs"]
            ros2.includedirs = ["axonvex_ros2/include"]
            ros2.requires = ["core"]

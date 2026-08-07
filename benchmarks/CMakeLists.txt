# Phase 3 WS-PERF microbenchmarks. Only built when AXONVEX_BUILD_BENCHMARKS=ON
# (see root CMakeLists.txt) -- these are run manually, not registered as
# ctest cases: they measure real wall-clock time and are meant to feed
# docs/benchmarks.md, not to gate CI pass/fail.
add_executable(axonvex_benchmarks
    schedulerJitterBenchmark.cpp
    portThroughputBenchmark.cpp
    pipelineLatencyBenchmark.cpp
    estopLatencyBenchmark.cpp
)
target_link_libraries(axonvex_benchmarks
    PRIVATE
        axonvex_core
        axonvex_safety
        benchmark::benchmark
        benchmark::benchmark_main
)

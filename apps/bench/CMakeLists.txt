# infinity-bench: release-mode microbenchmarks for the math and memory cores
# (rule 08): mat4/quat/vec3 (F1.6) and arena allocator (F2.10).
#
# Benchmarks measure, they never assert (rule 08), so this target is NOT
# registered with CTest and is not a correctness gate. It is built in every
# preset but its numbers are only meaningful in release (-O2, no sanitizers).
# The harness is self-contained (std::chrono only); doctest stays reserved for
# correctness tests (rule 06) and no dependency is vendored here (ADR-061/068).
add_executable(infinity-bench
    bench_harness.h
    bench_math.cpp
    bench_arena.cpp
    bench_triangle.cpp
)

target_link_libraries(infinity-bench PRIVATE infinity::math infinity::core infinity::renderer)

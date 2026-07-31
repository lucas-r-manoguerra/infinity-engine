// apps/bench/bench_math.cpp
//
// Release-mode microbenchmarks for the math core (F1.6, rule 08). Measures
// the per-operation cost of the hot math functions against the ROADMAP
// baselines to be revised in F1: mat4.mul ~34 ns, mat4.inverse ~18 ns,
// quat.slerp ~75 ns.
//
// Design notes:
// - Self-contained: std::chrono only, no third_party dependency (ADR-061/068);
//   doctest is for correctness tests (rule 06), not for benchmarks.
// - The math functions live in the infinity_math static library, a separate
//   translation unit, and are not inlined without LTO, so every call is the
//   real cost a library consumer pays. The build never applies fast-math
//   (ADR-056), so the numbers reflect the shipped math.
// - A volatile sink keeps every computed result observable: the optimizer can
//   neither eliminate the calls nor hoist loop-invariant ones. Portable, no
//   inline asm required.
// - Inputs rotate through precomputed rotations/transforms/vectors so the loop
//   never benchmarks a single constant (inputs are realistic object TRS
//   matrices and rotations, not identities).
// - Output is machine-parseable: `bench:<name>:<ns_per_op>` plus one human
//   summary line per metric.
//
// Benchmarks measure, they never assert (rule 08). Not a CTest target.

#include <array>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>

#include "infinity/math/mat4.h"
#include "infinity/math/quat.h"
#include "infinity/math/vec3.h"

namespace {

using infinity::math::Mat4;
using infinity::math::Quat;
using infinity::math::Vec3;

// Volatile sink: every measured result accumulates here so the optimizer must
// keep the computation and cannot prove it dead. Bench-only global, never used
// outside this translation unit.
volatile float g_sink = 0.0f;

void consume(float value) { g_sink += value; }

void consume(const Vec3& value) { g_sink += value.x; }

void consume(const Quat& value) { g_sink += value.x; }

void consume(const Mat4& value) { g_sink += value.m[0]; }

struct BenchmarkResult {
    const char* name;
    double nsPerOp;
};

// Runs fn(i) for every i in [0, measuredIters) and returns ns per operation.
// Warmup iterations are discarded; the result is kept observable via consume().
template <typename Fn>
BenchmarkResult runBenchmark(const char* name, size_t warmupIters, size_t measuredIters, Fn&& fn) {
    for (size_t i = 0; i < warmupIters; ++i) {
        consume(fn(i));
    }
    const auto start = std::chrono::steady_clock::now();
    for (size_t i = 0; i < measuredIters; ++i) {
        consume(fn(i));
    }
    const auto end = std::chrono::steady_clock::now();
    const double nsPerOp = std::chrono::duration<double, std::nano>(end - start).count() /
                           static_cast<double>(measuredIters);
    return {.name = name, .nsPerOp = nsPerOp};
}

// Prints the machine-parseable line and the human summary line.
void report(const BenchmarkResult& result, const char* humanName) {
    std::cout << "bench:" << result.name << ":" << std::fixed << std::setprecision(3)
              << result.nsPerOp << '\n';
    std::cout << std::setprecision(2) << humanName << ": " << result.nsPerOp << " ns/op\n";
}

} // namespace

int main() {
    // Realistic inputs: four object transforms (TRS) with distinct translations,
    // rotations and scales, four rotations as quaternions, four non-trivial
    // vectors, and a range of interpolation factors.
    const std::array<Mat4, 4> transforms = {
        Mat4::translation(Vec3{1.0f, 2.0f, 3.0f}) *
            Mat4::rotationYawPitchRoll(15.0f, -30.0f, 45.0f) * Mat4::scale(Vec3{1.0f, 1.0f, 1.0f}),
        Mat4::translation(Vec3{-4.0f, 0.5f, 8.0f}) *
            Mat4::rotationYawPitchRoll(120.0f, 60.0f, -15.0f) * Mat4::scale(Vec3{2.0f, 0.5f, 1.5f}),
        Mat4::translation(Vec3{0.25f, -1.0f, 5.0f}) *
            Mat4::rotationYawPitchRoll(-70.0f, 25.0f, 90.0f) * Mat4::scale(Vec3{1.0f, 3.0f, 1.0f}),
        Mat4::translation(Vec3{9.0f, 3.0f, -2.0f}) *
            Mat4::rotationYawPitchRoll(200.0f, -10.0f, 30.0f) * Mat4::scale(Vec3{0.5f, 2.0f, 2.0f}),
    };
    const std::array<Quat, 4> rotations = {
        Quat::fromYawPitchRoll(15.0f, -30.0f, 45.0f),
        Quat::fromYawPitchRoll(120.0f, 60.0f, -15.0f),
        Quat::fromYawPitchRoll(-70.0f, 25.0f, 90.0f),
        Quat::fromYawPitchRoll(200.0f, -10.0f, 30.0f),
    };
    const std::array<Vec3, 4> vectors = {
        Vec3{1.0f, 2.0f, 3.0f},
        Vec3{-4.0f, 0.5f, 8.0f},
        Vec3{0.25f, -1.0f, 5.0f},
        Vec3{9.0f, 3.0f, -2.0f},
    };
    const std::array<float, 4> slerpFactors = {0.0f, 0.33f, 0.67f, 1.0f};

    // Mat4 (60-150 FLOPs per op; 1M iterations keep timing well above noise).
    report(runBenchmark("mat4_mul", 100000, 1000000,
                        [&](size_t i) { return transforms[i % 4] * transforms[(i + 1) % 4]; }),
           "mat4.mul");
    report(runBenchmark("mat4_inverse", 20000, 200000,
                        [&](size_t i) { return transforms[i % 4].inverted(); }),
           "mat4.inverse");
    report(runBenchmark("mat4_transpose", 100000, 1000000,
                        [&](size_t i) { return transforms[i % 4].transposed(); }),
           "mat4.transpose");
    report(runBenchmark("mat4_transform_point", 100000, 1000000,
                        [&](size_t i) { return transforms[i % 4] * vectors[(i + 1) % 4]; }),
           "mat4.transform_point");

    // Quat (slerp uses acos + two sin + two sqrt; 200k iterations are enough).
    report(runBenchmark("quat_slerp", 20000, 200000,
                        [&](size_t i) {
                            return rotations[i % 4].slerp(rotations[(i + 1) % 4],
                                                          slerpFactors[i % 4]);
                        }),
           "quat.slerp");
    report(runBenchmark("quat_normalize", 100000, 1000000,
                        [&](size_t i) { return rotations[i % 4].normalized(); }),
           "quat.normalize");

    // Vec3 (a few FLOPs per op; 1M iterations amortize the harness overhead).
    report(runBenchmark("vec3_dot", 100000, 1000000,
                        [&](size_t i) { return vectors[i % 4].dot(vectors[(i + 1) % 4]); }),
           "vec3.dot");
    report(runBenchmark("vec3_cross", 100000, 1000000,
                        [&](size_t i) { return vectors[i % 4].cross(vectors[(i + 1) % 4]); }),
           "vec3.cross");
    report(runBenchmark("vec3_normalize", 100000, 1000000,
                        [&](size_t i) { return vectors[i % 4].normalized(); }),
           "vec3.normalize");

    return 0;
}

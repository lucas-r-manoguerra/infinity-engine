// apps/bench/bench_harness.h
//
// Shared harness for the release-mode microbenchmarks (rule 08). Used by the
// math benchmarks (F1.6) and the memory benchmarks (F2.10) against the ROADMAP
// baselines to be revised in each phase: mat4.mul ~34 ns, mat4.inverse ~18 ns,
// quat.slerp ~75 ns, arena alloc <60 ns.
//
// Design notes:
// - Self-contained: std::chrono only, no third_party dependency (ADR-061/068);
//   doctest is for correctness tests (rule 06), not for benchmarks.
// - A volatile sink keeps every computed result observable: the optimizer can
//   neither eliminate the calls nor hoist loop-invariant ones. Portable, no
//   inline asm required.
// - The pointer overload only makes the address observable (no dereference):
//   reading memory inside the measured region would add cache traffic that is
//   not part of the operation being measured.
// - Output is machine-parseable: `bench:<name>:<ns_per_op>` plus one human
//   summary line per metric.
//
// Benchmarks measure, they never assert (rule 08). Not a CTest target.
#pragma once

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>

#include "infinity/math/mat4.h"
#include "infinity/math/quat.h"
#include "infinity/math/vec3.h"

namespace bench {

// Volatile sink: every measured result accumulates here so the optimizer must
// keep the computation and cannot prove it dead. Bench-only global, never used
// outside the benchmark translation units.
inline volatile float g_sink = 0.0f;

// Pointer sink: makes the returned address observable without dereferencing,
// so allocation microbenchmarks keep the block's cache lines untouched.
inline void* g_voidSink = nullptr;

inline void consume(float value) { g_sink += value; }

inline void consume(const infinity::math::Vec3& value) { g_sink += value.x; }

inline void consume(const infinity::math::Quat& value) { g_sink += value.x; }

inline void consume(const infinity::math::Mat4& value) { g_sink += value.m[0]; }

inline void consume(void* ptr) { g_voidSink = ptr; }

struct BenchmarkResult {
    const char* name;
    double nsPerOp;
};

// Runs fn(i) for every i in [0, measuredIters) and returns ns per operation.
// Warmup iterations are discarded; the result is kept observable via consume().
template <typename Fn>
inline BenchmarkResult runBenchmark(const char* name, size_t warmupIters, size_t measuredIters,
                                    Fn&& fn) {
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
inline void report(const BenchmarkResult& result, const char* humanName) {
    std::cout << "bench:" << result.name << ":" << std::fixed << std::setprecision(3)
              << result.nsPerOp << '\n';
    std::cout << std::setprecision(2) << humanName << ": " << result.nsPerOp << " ns/op\n";
}

} // namespace bench

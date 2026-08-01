// apps/bench/bench_arena.cpp
//
// Release-mode microbenchmarks for the memory core (F2.10, rule 08). Measures
// the per-allocation cost of ArenaAllocator (F2.2, ADR-005) against the ROADMAP
// baseline to be revised in F2: arena alloc <60 ns.
//
// Design notes:
// - Self-contained: std::chrono only (via bench_harness.h), no third_party
//   dependency (ADR-061/068). Benchmarks measure, they never assert (rule 08).
//   Not a CTest target.
// - The bump path is pure pointer arithmetic over a block acquired once at
//   construction (no backing traffic, no per-request malloc, no touching of
//   the handed-out memory). 16 MiB would exhaust in benchmark A's measured
//   region (1M ops, ~128 B/op average); the block is sized so the measured
//   region never exhausts and every iteration measures the bump path.
// - Benchmark A (arena.alloc) measures the pure bump path with rotating
//   sizes/alignments; the arena is never reset in the loop.
// - Benchmark B (arena.reset+alloc) measures frame-scoped steady state: the
//   arena resets every 128 ops, as an engine frame arena would between frames.
// - ArenaAllocator::deallocate() is a no-op by design (release happens at
//   reset()/destruction), so no alloc/free pair is measurable; it is omitted.
// - Output keeps the machine-parseable `bench:<name>:<ns_per_op>` format.
//
// Benchmarks measure, they never assert (rule 08). Not a CTest target.

#include <array>
#include <cstddef>
#include <cstdint>

#include "bench_harness.h"
#include "infinity/core/arena_allocator.h"

// Entry point for the memory suite. bench_math.cpp defines main() and calls
// this after the math suite, so both run under the single infinity-bench
// executable (an executable may have exactly one main()).
int runArenaBenchmarks() {
    // Sized so benchmark A's 1M allocations (sizes {32,64,128,256}, aligned
    // {8,8,16,64}) never exhaust the block; bump allocations do not touch
    // memory, so the block costs virtual address space only.
    infinity::core::ArenaAllocator arena(size_t{512} * 1024 * 1024);

    // Realistic frame workload: rotating request sizes and alignments cover
    // transform buffers (16-byte SIMD), per-entity scratch and pool blocks.
    const std::array<size_t, 4> sizes = {32, 64, 128, 256};
    const std::array<size_t, 4> alignments = {8, 8, 16, 64};

    // Pure bump path, no reset in the loop: every op is the same steady-state
    // offset bump. 1M iterations keep timing well above noise.
    bench::report(bench::runBenchmark(
                      "arena_alloc", 100000, 1000000,
                      [&](size_t i) { return arena.allocate(sizes[i % 4], alignments[i % 4]); }),
                  "arena.alloc");

    // Frame-scoped usage: the hot path allocates per frame and the arena is
    // reset every 128 ops, so the reset cost is amortized like the real engine
    // frame loop. 500k iterations with a reset every 128 ops.
    bench::report(bench::runBenchmark("arena_reset_alloc", 50000, 500000,
                                      [&](size_t i) {
                                          if (i % 128 == 0) {
                                              arena.reset();
                                          }
                                          return arena.allocate(64, 16);
                                      }),
                  "arena.reset+alloc");

    return 0;
}

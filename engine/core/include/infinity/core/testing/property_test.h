// infinity/core/testing/property_test.h
//
// In-house property-based testing + fuzzing base (F2.9, ADR-017, rule 06).
// The engine has no exceptions (rule 04) and demands reproducible, auditable
// random testing (rule 11), so instead of pulling in a heavyweight property
// library this is a tiny header: a seeded std::mt19937 stream, a few value
// generators, and a runner that replays the seed to report the exact failing
// case.
//
//   Determinism - The engine is std::mt19937 seeded with an explicit value and
//                 never draws from time or environment (rule 11). The runner
//                 snapshots the engine state at every case boundary and hands
//                 that snapshot to the property, so the case index IS the
//                 minimum failing case: re-running with the same seed replays
//                 the identical stream and reproduces the failure. The state
//                 machine of mt19937 is mandated by the standard and produces
//                 the same values on every platform; the uniform_*_distribution
//                 conversion methods are implementation-defined, so a fixed
//                 seed is portable within one toolchain — the same convention
//                 the existing math property tests already use (rule 06).
//   NaN policy  - f32(rng) draws arbitrary FINITE bit patterns only (it rejects
//                 the all-ones exponent field, i.e. NaN and Inf). Per ADR-056
//                 the math code has an explicit NaN/Inf policy: a matrix or
//                 quaternion containing NaN/Inf is UNDEFINED input and its
//                 result is unspecified, so the generic generator stays in the
//                 defined domain. Bounded f32(rng, min, max) covers the
//                 well-conditioned inputs a property needs to express.
//   Exceptions  - This harness never throws (rule 04). runForAll reports plain
//                 values; checkForAll emits one doctest failure. It compiles
//                 under -fno-exceptions and DOCTEST_CONFIG_NO_EXCEPTIONS.
//   Allocation  - runForAll/checkForAll allocate nothing beyond the per-case
//                 RNG snapshot copy. Only the data-producing generators
//                 (bytes) allocate, and only for the buffer the caller asked
//                 for.
//
// TEST-ONLY FACILITY. This header lives under include/infinity/core/testing/
// and is intended for test translation units only; no production header
// references it. It depends on doctest (for checkForAll's CHECK_MESSAGE), so
// an accidental include from production code fails to compile — which is
// exactly what a test-only facility should do.
#pragma once

#include <doctest/doctest.h>

#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace infinity::core::testing {

// Seedable engine backing every property run.
using PropertyRng = std::mt19937;

// Uniform int32_t in the INCLUSIVE range [min, max]. min < max is a
// programming invariant of the caller (assert in debug, rule 04).
[[nodiscard]] inline int32_t i32(PropertyRng& rng, int32_t min, int32_t max) {
    assert(min < max);
    return std::uniform_int_distribution<int32_t>{min, max}(rng);
}

// Arbitrary FINITE float bit pattern (NaN and Inf excluded; see the header
// brief for the ADR-056 rationale). Covers subnormals, negative zero and the
// full finite exponent range, so math code sees realistic float diversity.
[[nodiscard]] inline float f32(PropertyRng& rng) {
    for (;;) {
        // NOLINTBEGIN(modernize-use-auto): rule 02 forbids auto for trivial types
        const std::uint32_t bits = static_cast<std::uint32_t>(rng());
        const float value = std::bit_cast<float>(bits);
        // NOLINTEND(modernize-use-auto)
        if (std::isfinite(value)) {
            return value;
        }
    }
}

// Uniform float in [min, max) for well-conditioned inputs. min < max is a
// programming invariant of the caller (assert in debug, rule 04).
[[nodiscard]] inline float f32(PropertyRng& rng, float min, float max) {
    assert(min < max);
    return std::uniform_real_distribution<float>{min, max}(rng);
}

// Uniform coin flip.
[[nodiscard]] inline bool coin(PropertyRng& rng) {
    return std::uniform_int_distribution<int>{0, 1}(rng) != 0;
}

// `count` random bytes, for feeding decoders and other byte-level fuzz
// targets. count == 0 yields an empty buffer.
[[nodiscard]] inline std::vector<std::uint8_t> bytes(PropertyRng& rng, std::size_t count) {
    std::vector<std::uint8_t> buffer;
    buffer.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        buffer[i] = static_cast<std::uint8_t>(rng() & 0xFFu);
    }
    return buffer;
}

// Result of a property run. When passed is false, failingCase is the 0-based
// index of the first case where the property did not hold, and re-running with
// the same seed reproduces it exactly (rule 11). When passed is true,
// failingCase equals numCases: the run exhausted every case.
struct PropertyOutcome {
    bool passed;
    std::size_t failingCase;
    PropertyRng::result_type seed;
};

// Runs fn once per case from a seed-seeded engine. Before each case the engine
// state is snapshotted (a cheap copy) and passed to fn, so every case sees the
// deterministic stream for that index; the main stream then advances by
// whatever fn consumed. Stops at the first failing case and reports its index.
// Never throws and allocates nothing beyond the snapshot copy.
template <typename Fn>
[[nodiscard]] PropertyOutcome runForAll(PropertyRng::result_type seed, std::size_t numCases,
                                        Fn&& fn) {
    PropertyRng rng{seed};
    for (std::size_t caseIndex = 0; caseIndex < numCases; ++caseIndex) {
        PropertyRng caseRng = rng; // per-case snapshot
        if (!fn(caseRng)) {
            return {.passed = false, .failingCase = caseIndex, .seed = seed};
        }
        rng = caseRng;
    }
    return {.passed = true, .failingCase = numCases, .seed = seed};
}

// Convenience wrapper: runs runForAll and, on the first failing case, emits a
// single doctest failure identifying the exact case and seed (re-run with that
// seed to debug). Emits nothing on success. Stops after the first failure — it
// never floods the log with N messages.
template <typename Fn>
void checkForAll(PropertyRng::result_type seed, std::size_t numCases, Fn&& fn) {
    const PropertyOutcome outcome = runForAll(seed, numCases, fn);
    if (outcome.passed) {
        return;
    }
    CHECK_MESSAGE(false, "Property failed at case ", outcome.failingCase, " (0-based) with seed ",
                  outcome.seed);
}

} // namespace infinity::core::testing

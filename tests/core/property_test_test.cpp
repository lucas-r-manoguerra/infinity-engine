// tests/core/property_test_test.cpp
//
// Self-test of the property-testing harness (F2.9, ADR-017). The contract the
// harness must honor (rule 06/11): a property that holds passes every case, a
// failing property stops at the exact first failing case, the same seed
// reproduces the same outcome, and the generators respect their documented
// bounds.
#include "infinity/core/testing/property_test.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

using infinity::core::testing::bytes;
using infinity::core::testing::coin;
using infinity::core::testing::f32;
using infinity::core::testing::i32;
using infinity::core::testing::PropertyOutcome;
using infinity::core::testing::PropertyRng;
using infinity::core::testing::runForAll;

TEST_CASE("runForAll reports a property that holds for every case") {
    const PropertyOutcome outcome = runForAll(20260731u, 100, [](PropertyRng&) { return true; });
    CHECK(outcome.passed);
    CHECK(outcome.failingCase == 100);
    CHECK(outcome.seed == 20260731u);
}

TEST_CASE("runForAll reports the exact first failing case and stops") {
    std::size_t calls = 0;
    const PropertyOutcome outcome = runForAll(7u, 50, [&calls](PropertyRng&) {
        ++calls;
        return calls != 13;
    });
    CHECK_FALSE(outcome.passed);
    CHECK(outcome.failingCase == 12);
    CHECK(calls == 13);
    CHECK(outcome.seed == 7u);
}

TEST_CASE("runForAll with the same seed reproduces the same outcome") {
    const auto failing = [](PropertyRng& rng) { return i32(rng, 0, 3) != 3; };
    const PropertyOutcome first = runForAll(20260201u, 100, failing);
    const PropertyOutcome second = runForAll(20260201u, 100, failing);
    CHECK(first.passed == second.passed);
    CHECK(first.failingCase == second.failingCase);
    CHECK(first.seed == second.seed);
}

TEST_CASE("runForAll with a different seed draws a different stream") {
    const auto draw = [](PropertyRng& rng) { return i32(rng, 0, 3); };
    PropertyRng first{20260201u};
    PropertyRng second{20260202u};
    bool sameStream = true;
    for (int i = 0; i < 64; ++i) {
        if (draw(first) != draw(second)) {
            sameStream = false;
            break;
        }
    }
    CHECK_FALSE(sameStream);
}

TEST_CASE("i32 stays within its inclusive bounds") {
    PropertyRng rng{1u};
    for (int i = 0; i < 1000; ++i) {
        const int32_t value = i32(rng, -7, 13);
        CHECK(value >= -7);
        CHECK(value <= 13);
    }
}

TEST_CASE("i32 reaches every value of a small inclusive range") {
    PropertyRng rng{2u};
    std::array<int, 5> seen{};
    for (int i = 0; i < 10000; ++i) {
        ++seen[i32(rng, 0, 4)];
    }
    for (const int count : seen) {
        CHECK(count > 0);
    }
}

TEST_CASE("f32 with bounds stays finite within the range") {
    PropertyRng rng{3u};
    for (int i = 0; i < 1000; ++i) {
        const float value = f32(rng, -1.5f, 2.5f);
        CHECK(std::isfinite(value));
        CHECK(value >= -1.5f);
        CHECK(value <= 2.5f);
    }
}

TEST_CASE("f32 arbitrary bit patterns are always finite") {
    PropertyRng rng{4u};
    for (int i = 0; i < 10000; ++i) {
        CHECK(std::isfinite(f32(rng)));
    }
}

TEST_CASE("coin yields both outcomes") {
    PropertyRng rng{5u};
    int trues = 0;
    int falses = 0;
    for (int i = 0; i < 10000; ++i) {
        if (coin(rng)) {
            ++trues;
        } else {
            ++falses;
        }
    }
    CHECK(trues > 0);
    CHECK(falses > 0);
}

TEST_CASE("bytes yields buffers of the requested size") {
    PropertyRng rng{6u};
    const std::vector<std::uint8_t> empty = bytes(rng, 0);
    CHECK(empty.empty());
    for (const std::size_t count : {1u, 4u, 64u}) {
        const std::vector<std::uint8_t> buffer = bytes(rng, count);
        CHECK(buffer.size() == count);
    }
}

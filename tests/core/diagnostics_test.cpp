// tests/core/diagnostics_test.cpp
//
// Diagnostics contract tests (F2.6, rule 08): the counter catalog is fixed
// and zeroed at construction, increment/set/value are exact and independent,
// concurrent increments lose no updates, reset zeroes everything, and
// instance() is a stable process-wide singleton. Every case uses a LOCAL
// instance (never instance()) so no test shares state with another (rule 11).
//
// The mixed case pins the documented semantics: increment is an atomic add and
// set is an atomic store - no read-modify-write ordering is guaranteed across
// threads, so the case asserts only the single-threaded call sequence.
//
// The concurrency case reads the counter after std::thread::join, which
// happens-before the main thread's load: with relaxed atomics (see
// diagnostics.h) the full count is guaranteed visible.
#include "infinity/core/diagnostics.h"

#include <chrono>
#include <cstdint>
#include <thread>

#include <doctest/doctest.h>

namespace {

using infinity::core::COUNTER_COUNT;
using infinity::core::CounterId;
using infinity::core::Diagnostics;

constexpr uint64_t THREAD_COUNT = 4;
constexpr uint64_t INCREMENTS_PER_THREAD = 10000;
constexpr uint64_t CONCURRENT_TOTAL = THREAD_COUNT * INCREMENTS_PER_THREAD;

// Loose upper bound (seconds) for 4 threads x 10k relaxed atomic increments:
// microseconds in practice, wide headroom so a loaded CI runner can never trip
// it (same pattern as time_test.cpp's CI-safe bounds).
constexpr double CONCURRENT_SECONDS_BOUND = 1.0;

// Gauge value for the set/read case: a plausible 4K frame pixel count. The
// multiplication is done in uint64_t so no implicit widening occurs.
constexpr uint64_t RENDER_PIXELS_GAUGE = static_cast<uint64_t>(4096) * 2304;

// Bumps counter id count times. Each thread of the concurrency test runs one
// of these; the final value must be the exact sum (relaxed atomic increments
// never lose an update).
void hammerCounter(Diagnostics& diagnostics, CounterId id, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) {
        diagnostics.increment(id);
    }
}

} // namespace

TEST_CASE("all counters start at zero") {
    const Diagnostics diagnostics;
    for (size_t i = 0; i < COUNTER_COUNT; ++i) {
        CAPTURE(i);
        CHECK(diagnostics.value(static_cast<CounterId>(i)) == 0);
    }
}

TEST_CASE("increment adds one by default and honors an explicit amount") {
    Diagnostics diagnostics;
    diagnostics.increment(CounterId::FRAME_COUNT);
    CHECK(diagnostics.value(CounterId::FRAME_COUNT) == 1);

    diagnostics.increment(CounterId::FRAME_COUNT, 5);
    CHECK(diagnostics.value(CounterId::FRAME_COUNT) == 6);
}

TEST_CASE("set stores a gauge and value reads it back") {
    Diagnostics diagnostics;
    diagnostics.set(CounterId::RENDER_PIXELS, RENDER_PIXELS_GAUGE);
    CHECK(diagnostics.value(CounterId::RENDER_PIXELS) == RENDER_PIXELS_GAUGE);
}

TEST_CASE("concurrent increments from four threads lose no updates") {
    Diagnostics diagnostics;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    std::thread first{
        [&] { hammerCounter(diagnostics, CounterId::FRAME_COUNT, INCREMENTS_PER_THREAD); }};
    std::thread second{
        [&] { hammerCounter(diagnostics, CounterId::FRAME_COUNT, INCREMENTS_PER_THREAD); }};
    std::thread third{
        [&] { hammerCounter(diagnostics, CounterId::FRAME_COUNT, INCREMENTS_PER_THREAD); }};
    std::thread fourth{
        [&] { hammerCounter(diagnostics, CounterId::FRAME_COUNT, INCREMENTS_PER_THREAD); }};
    first.join();
    second.join();
    third.join();
    fourth.join();

    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(diagnostics.value(CounterId::FRAME_COUNT) == CONCURRENT_TOTAL);
    CHECK(elapsedSeconds < CONCURRENT_SECONDS_BOUND);
}

TEST_CASE("counters are independent") {
    Diagnostics diagnostics;
    diagnostics.increment(CounterId::FRAME_COUNT, 7);
    CHECK(diagnostics.value(CounterId::FRAME_COUNT) == 7);
    CHECK(diagnostics.value(CounterId::FIXED_UPDATES) == 0);
    CHECK(diagnostics.value(CounterId::RENDER_DRAWS) == 0);
}

TEST_CASE("reset zeroes every counter") {
    Diagnostics diagnostics;
    for (size_t i = 0; i < COUNTER_COUNT; ++i) {
        diagnostics.increment(static_cast<CounterId>(i));
    }
    CHECK(diagnostics.value(CounterId::FRAME_COUNT) != 0);

    diagnostics.reset();

    for (size_t i = 0; i < COUNTER_COUNT; ++i) {
        CAPTURE(i);
        CHECK(diagnostics.value(static_cast<CounterId>(i)) == 0);
    }
}

TEST_CASE("every counter id maps to a valid array slot") {
    Diagnostics diagnostics;
    for (size_t i = 0; i < COUNTER_COUNT; ++i) {
        // NOLINTNEXTLINE(modernize-use-auto) -- rule 02: no auto for trivial types.
        const CounterId id = static_cast<CounterId>(i);
        diagnostics.increment(id);
        diagnostics.set(id, 3);
        CAPTURE(i);
        CHECK(diagnostics.value(id) == 3);
    }
}

TEST_CASE("instance returns a stable process-wide diagnostics") {
    CHECK(&Diagnostics::instance() == &Diagnostics::instance());
}

TEST_CASE("increment and set compose in call order") {
    Diagnostics diagnostics;
    diagnostics.increment(CounterId::RENDER_DRAWS);
    diagnostics.set(CounterId::RENDER_DRAWS, 10);
    diagnostics.increment(CounterId::RENDER_DRAWS);
    CHECK(diagnostics.value(CounterId::RENDER_DRAWS) == 11);
}

TEST_CASE("value is usable in a tight loop") {
    constexpr uint64_t READS = 100000;
    Diagnostics diagnostics;
    diagnostics.set(CounterId::FRAME_COUNT, 42);

    uint64_t sum = 0;
    for (uint64_t i = 0; i < READS; ++i) {
        sum += diagnostics.value(CounterId::FRAME_COUNT);
    }

    CHECK(sum == READS * 42);
}

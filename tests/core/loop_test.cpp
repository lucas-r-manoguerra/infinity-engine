// tests/core/loop_test.cpp
//
// Loop contract tests (F2.5 part 2, ADR-006, rules 08 and 11): the fixed-
// timestep loop runs updates at exactly fixedDeltaSeconds, decoupling logic
// from the real frame rate through an accumulator. Determinism is the
// contract: step() is a pure function of its inputs and state, so every case
// here drives the loop with synthetic deltas - no sleeps, no wall time, no
// clock. The same accumulated total must yield the same update count however
// it is framed (ADR-006: same simulation at different framerates).
#include "infinity/core/loop.h"

#include <type_traits>

#include <doctest/doctest.h>

namespace {

constexpr double SIXTIETH_SECOND = 1.0 / 60.0;
constexpr double CENT_TWENTIETH_SECOND = 1.0 / 120.0;
constexpr double HUNDRED_EIGHTIETH_SECOND = 1.0 / 180.0;

// Relative-tolerance comparison: 1e-12 matches the accumulation tolerance of
// the loop (repeated double addition, ADR-056) with headroom for a loaded CI
// runner. The loop is pure double math, so the comparison is exactness-bound,
// not timing-bound.
doctest::Approx near(double value) { return doctest::Approx(value).epsilon(1e-12); }

// The loop consumes whole fixed steps and leaves a sub-fixed residue that is
// ~1e-17 in practice; assert against an absolute bound instead of a relative
// Approx, whose tolerance vanishes as the expected value approaches zero.
constexpr double RESIDUE_BOUND = 1e-9;

// Test-harness state captured by the update callback. File-scope because the
// callback is a function pointer (no captures, rule 08); this is test
// instrumentation, not engine state (rule 11).
int g_updateCount = 0;
double g_deltaSumSeconds = 0.0;
double g_lastDeltaSeconds = 0.0;
double g_lastElapsedSeconds = 0.0;

void recordUpdate(infinity::core::Time& time) noexcept {
    ++g_updateCount;
    g_deltaSumSeconds += time.deltaSeconds();
    g_lastDeltaSeconds = time.deltaSeconds();
    g_lastElapsedSeconds = time.elapsedSeconds();
}

void resetRecorder() {
    g_updateCount = 0;
    g_deltaSumSeconds = 0.0;
    g_lastDeltaSeconds = 0.0;
    g_lastElapsedSeconds = 0.0;
}

// Fresh, recorded harness: resets the recorder and installs the callback on
// the given loop so each case starts clean.
void instrument(infinity::core::Loop& loop) {
    resetRecorder();
    loop.setUpdateCallback(recordUpdate);
}

// The loop cannot be default-constructed (a fixed timestep is mandatory) nor
// copied or moved (it owns simulation state; allocator convention, rule 03).
static_assert(!std::is_default_constructible_v<infinity::core::Loop>);
static_assert(!std::is_copy_constructible_v<infinity::core::Loop>);
static_assert(!std::is_move_constructible_v<infinity::core::Loop>);

} // namespace

TEST_CASE("A single exact fixed step runs exactly one update") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    loop.step(SIXTIETH_SECOND);

    CHECK(g_updateCount == 1);
    CHECK(g_lastDeltaSeconds == near(SIXTIETH_SECOND));
    CHECK(loop.time().deltaSeconds() == near(SIXTIETH_SECOND));
    CHECK(loop.time().elapsedSeconds() == near(SIXTIETH_SECOND));
    CHECK(loop.accumulator() < RESIDUE_BOUND);
    CHECK(loop.alpha() < RESIDUE_BOUND);
}

TEST_CASE("A half fixed step runs no updates and keeps the remainder") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    loop.step(CENT_TWENTIETH_SECOND);

    CHECK(g_updateCount == 0);
    CHECK(loop.accumulator() == near(CENT_TWENTIETH_SECOND));
    CHECK(loop.alpha() == near(0.5));
    CHECK(loop.time().elapsedSeconds() == near(0.0));
}

TEST_CASE("Two half steps accumulate into exactly one update") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    loop.step(CENT_TWENTIETH_SECOND);
    CHECK(g_updateCount == 0);
    loop.step(CENT_TWENTIETH_SECOND);

    CHECK(g_updateCount == 1);
    CHECK(g_lastDeltaSeconds == near(SIXTIETH_SECOND));
    CHECK(loop.time().elapsedSeconds() == near(SIXTIETH_SECOND));
    CHECK(loop.accumulator() < RESIDUE_BOUND);
}

TEST_CASE("A three-fixed-step real delta runs exactly three updates") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    loop.step(3.0 * SIXTIETH_SECOND);

    CHECK(g_updateCount == 3);
    CHECK(g_deltaSumSeconds == near(3.0 * SIXTIETH_SECOND));
    CHECK(loop.time().elapsedSeconds() == near(3.0 * SIXTIETH_SECOND));
    CHECK(loop.accumulator() < RESIDUE_BOUND);
}

TEST_CASE("The update callback always observes the fixed delta") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    loop.step(10.0 * SIXTIETH_SECOND);

    CHECK(g_updateCount == 10);
    CHECK(g_deltaSumSeconds == near(10.0 * SIXTIETH_SECOND));
    CHECK(g_lastDeltaSeconds == near(SIXTIETH_SECOND));
}

TEST_CASE("Elapsed time accumulates exactly across updates") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    for (int i = 0; i < 13; ++i) {
        loop.step(SIXTIETH_SECOND);
    }

    CHECK(g_updateCount == 13);
    CHECK(loop.time().elapsedSeconds() == near(13.0 * SIXTIETH_SECOND));
}

TEST_CASE("Spiral-of-death clamp bounds catch-up work on a huge delta") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);
    loop.setMaxFrameDelta(0.1);
    CHECK(loop.maxFrameDelta() == near(0.1));

    loop.step(10.0);

    // ceil(0.1 / (1/60)) + 1 = 7 is the documented worst case; the observed
    // count (6) is far below the 600 updates that 10.0 s would otherwise owe.
    CHECK(g_updateCount <= 7);
    CHECK(g_updateCount >= 1);
    CHECK(loop.accumulator() >= 0.0);
    CHECK(loop.accumulator() <= 0.1 + SIXTIETH_SECOND);
}

TEST_CASE("reset restarts accumulation and engine time") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    loop.step(SIXTIETH_SECOND);
    loop.step(SIXTIETH_SECOND);
    CHECK(g_updateCount == 2);

    loop.reset();
    instrument(loop);
    loop.step(SIXTIETH_SECOND);

    CHECK(g_updateCount == 1);
    CHECK(loop.time().elapsedSeconds() == near(SIXTIETH_SECOND));
    CHECK(loop.accumulator() < RESIDUE_BOUND);
}

TEST_CASE("alpha stays within [0, 1] as the interpolation factor") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    loop.step(CENT_TWENTIETH_SECOND);
    CHECK(loop.alpha() == near(0.5));
    CHECK(loop.alpha() >= 0.0);
    CHECK(loop.alpha() <= 1.0);

    loop.step(CENT_TWENTIETH_SECOND);
    CHECK(loop.alpha() < RESIDUE_BOUND);

    loop.step(CENT_TWENTIETH_SECOND);
    CHECK(loop.alpha() == near(0.5));
    CHECK(loop.alpha() >= 0.0);
    CHECK(loop.alpha() <= 1.0);
}

// Negative real deltas are asserted in debug (time cannot flow backwards,
// ADR-006) and are not testable without NDEBUG; step(0.0) is the safe,
// release-visible boundary and is covered below.
TEST_CASE("Zero real delta runs no updates") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    loop.step(0.0);

    CHECK(g_updateCount == 0);
    CHECK(loop.accumulator() < RESIDUE_BOUND);
    CHECK(loop.time().elapsedSeconds() == near(0.0));
}

// setUpdateCallback(nullptr) is asserted in debug (ADR-003); the unset
// default callback no-ops in every build, which this case exercises.
TEST_CASE("A loop without an update callback still advances fixed steps") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());

    loop.step(3.0 * SIXTIETH_SECOND);

    CHECK(loop.time().elapsedSeconds() == near(3.0 * SIXTIETH_SECOND));
    CHECK(loop.accumulator() < RESIDUE_BOUND);
}

TEST_CASE("The callback observes Time advanced by the current update") {
    infinity::core::Loop loop(infinity::core::Loop::defaultFixedDelta());
    instrument(loop);

    loop.step(3.0 * SIXTIETH_SECOND);

    // The third callback observed elapsed == 3 * fixed: Time is advanced
    // BEFORE the callback runs, so the read inside reflects THIS update.
    CHECK(g_updateCount == 3);
    CHECK(g_lastElapsedSeconds == near(3.0 * SIXTIETH_SECOND));
    CHECK(g_lastDeltaSeconds == near(SIXTIETH_SECOND));
}

TEST_CASE("Equal accumulated totals yield equal update counts (determinism, ADR-006)") {
    // Three framings of the same one-fixed-step total must all produce exactly
    // one update: the update count depends only on the accumulated time, not
    // on how the frames were shaped (rule 11).
    infinity::core::Loop single(infinity::core::Loop::defaultFixedDelta());
    instrument(single);
    single.step(SIXTIETH_SECOND);
    CHECK(g_updateCount == 1);

    infinity::core::Loop split(infinity::core::Loop::defaultFixedDelta());
    instrument(split);
    split.step(CENT_TWENTIETH_SECOND);
    split.step(CENT_TWENTIETH_SECOND);
    CHECK(g_updateCount == 1);

    infinity::core::Loop thirds(infinity::core::Loop::defaultFixedDelta());
    instrument(thirds);
    thirds.step(HUNDRED_EIGHTIETH_SECOND);
    thirds.step(HUNDRED_EIGHTIETH_SECOND);
    thirds.step(HUNDRED_EIGHTIETH_SECOND);
    CHECK(g_updateCount == 1);
}

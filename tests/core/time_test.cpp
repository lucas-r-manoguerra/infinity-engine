// tests/core/time_test.cpp
//
// Clock and Time contract tests (F2.5, ADR-006, rules 07 and 11): the Clock
// measures monotonic wall time from its construction epoch and re-zeros on
// reset; Time is a passive, zeroed holder that stores exactly the values it
// is given and derives fps from the current delta with a defined
// division-by-zero policy.
//
// Wall-time cases use loose, CI-safe bounds: they assert lower bounds against
// real sleeps, never exact timings, so a loaded runner cannot trip them.
#include "infinity/core/time.h"

#include <chrono>
#include <thread>

#include <doctest/doctest.h>

namespace {

// Upper bound (seconds) for "right after construction/reset" reads: the
// microseconds a construction actually costs, with wide headroom so a loaded
// CI runner can never trip it.
constexpr double SMALL_SECONDS_BOUND = 0.1;

// The 60 Hz fixed-timestep frame duration (ADR-006), computed the same way
// the loop will: 1.0 / 60.0.
constexpr double SIXTIETH_SECOND = 1.0 / 60.0;

// Approx with a tight relative tolerance: a 1/60 delta inverts to 59.999...
// fps, so fps checks still compare within a few ulps of the target.
doctest::Approx near(double value) { return doctest::Approx(value).epsilon(1e-9); }

} // namespace

TEST_CASE("Clock starts at the moment of construction") {
    infinity::core::Clock clock;
    const double seconds = clock.elapsedSeconds();
    CHECK(seconds >= 0.0);
    CHECK(seconds < SMALL_SECONDS_BOUND);
}

TEST_CASE("Clock advances with wall time") {
    infinity::core::Clock clock;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(clock.elapsedMilliseconds() >= 15.0);
}

TEST_CASE("Clock readings never go backwards") {
    infinity::core::Clock clock;
    const double first = clock.elapsedSeconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const double second = clock.elapsedSeconds();
    CHECK(second >= first);
}

TEST_CASE("reset re-zeros the epoch") {
    infinity::core::Clock clock;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(clock.elapsedSeconds() >= 0.01);

    clock.reset();
    const double seconds = clock.elapsedSeconds();
    CHECK(seconds >= 0.0);
    CHECK(seconds < SMALL_SECONDS_BOUND);
}

TEST_CASE("Time is zeroed on construction") {
    const infinity::core::Time time;
    CHECK(time.deltaSeconds() == 0.0);
    CHECK(time.elapsedSeconds() == 0.0);
    CHECK(time.fps() == 0.0);
}

TEST_CASE("setDeltaSeconds of a 60 Hz frame yields 60 fps") {
    infinity::core::Time time;
    time.setDeltaSeconds(SIXTIETH_SECOND);
    CHECK(time.deltaSeconds() == near(SIXTIETH_SECOND));
    CHECK(time.fps() == near(60.0));
}

TEST_CASE("fps with a zero delta is 0, never inf or NaN") {
    infinity::core::Time time;
    CHECK(time.fps() == 0.0);

    time.setDeltaSeconds(0.0);
    CHECK(time.fps() == 0.0);
}

TEST_CASE("advance sets the delta and accumulates it into elapsed") {
    infinity::core::Time time;
    time.setElapsedSeconds(1.0);
    time.advance(0.016);

    CHECK(time.deltaSeconds() == near(0.016));
    CHECK(time.elapsedSeconds() == near(1.016));
}

TEST_CASE("setElapsedSeconds stores directly and advance accumulates from there") {
    infinity::core::Time time;
    time.setElapsedSeconds(2.5);
    CHECK(time.elapsedSeconds() == near(2.5));

    time.advance(SIXTIETH_SECOND);
    CHECK(time.elapsedSeconds() == near(2.5 + SIXTIETH_SECOND));
    CHECK(time.deltaSeconds() == near(SIXTIETH_SECOND));
}

TEST_CASE("fps is stable across representative frame deltas") {
    constexpr double CENT_TWENTIETH_SECOND = 1.0 / 120.0;
    constexpr double THIRTIETH_SECOND = 1.0 / 30.0;

    infinity::core::Time time;
    time.setDeltaSeconds(CENT_TWENTIETH_SECOND);
    CHECK(time.fps() == near(120.0));
    time.setDeltaSeconds(THIRTIETH_SECOND);
    CHECK(time.fps() == near(30.0));
    time.setDeltaSeconds(SIXTIETH_SECOND);
    CHECK(time.fps() == near(60.0));
}

TEST_CASE("Clock measures wall time that Time consumes as a frame delta") {
    infinity::core::Clock clock;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const double measured = clock.elapsedSeconds();
    CHECK(measured >= 0.01);

    infinity::core::Time time;
    time.advance(measured);
    CHECK(time.deltaSeconds() == near(measured));
    CHECK(time.elapsedSeconds() >= measured);
}

// infinity/core/time.h
//
// Deterministic time sources (F2.5, ADR-006, rule 11). This is the clock half
// of the fixed-timestep work: a monotonic wall-clock for measuring elapsed
// real time, and a passive state holder for engine time. The Loop (60 Hz
// fixed-timestep accumulation, ADR-006) is a separate component built on top
// of these in F2.5 part 2.
//
//   Clock - Monotonic wall-clock backed by std::chrono::steady_clock. Elapsed
//           time is measured from an epoch captured at construction (or the
//           last reset()). steady_clock is guaranteed monotonic and immune to
//           wall-clock adjustments, so elapsed time never goes backwards
//           (rule 11: never system_clock for elapsed measurement).
//   Time  - Passive engine-time state: the current frame's delta in seconds
//           and the total elapsed seconds since an external origin. It stores
//           exactly what it is given and never fabricates values; the Loop
//           owns the accumulation policy.
//
// Determinism (rule 11): no hidden state - both types live in explicit
// objects. A Clock is monotonic by construction (steady_clock guarantee);
// Time is passive, so monotonic updates are the Loop's responsibility and are
// asserted in debug (ADR-003).
#pragma once

#include <chrono>

namespace infinity::core {

// Monotonic wall-clock. Measures elapsed real time since construction or the
// last reset(). Backed by steady_clock; safe to call every frame (reads are
// cheap and allocation-free, rule 03).
class Clock {
public:
    // The epoch is the moment of construction: elapsed time is measured from
    // here until now (or the last reset()).
    Clock() noexcept;

    // Seconds since construction/last reset(). Never negative: steady_clock
    // is monotonic (rule 11).
    [[nodiscard]] double elapsedSeconds() const noexcept;

    // Milliseconds since construction/last reset() (convenience wrapper).
    [[nodiscard]] double elapsedMilliseconds() const noexcept;

    // Re-zeroes the epoch to the current steady_clock time.
    void reset() noexcept;

private:
    std::chrono::steady_clock::time_point m_epoch;
};

// Passive engine-time state (the object the loop updates). Holds the current
// frame's delta and the total elapsed time; neither is derived from wall time
// - the loop feeds both. Division-by-zero policy (rule 07): a non-positive
// delta yields fps() == 0, never inf/NaN. Negative deltas and backwards
// elapsed updates are programming errors (time cannot flow backwards in a
// fixed-timestep loop, ADR-006): asserted in debug, stored as-is in release
// (ADR-003).
class Time {
public:
    // Zeroed state: no delta, no elapsed time, fps 0 until a delta is set.
    Time() noexcept = default;

    // Sets the current frame's delta in seconds. Negative deltas are asserted
    // in debug. Time never fabricates a delta: the value read back equals
    // exactly what was set.
    void setDeltaSeconds(double deltaSeconds) noexcept;

    // The current frame's delta in seconds.
    [[nodiscard]] double deltaSeconds() const noexcept;

    // Sets the total elapsed time since the engine origin. Backwards updates
    // (lower than the current value) are asserted in debug: the loop
    // guarantees monotonic growth.
    void setElapsedSeconds(double elapsedSeconds) noexcept;

    // Total elapsed time since the engine origin.
    [[nodiscard]] double elapsedSeconds() const noexcept;

    // Frames per second implied by the current delta: 1 / delta. A
    // non-positive delta yields 0, never inf/NaN (division-by-zero policy,
    // rule 07).
    [[nodiscard]] double fps() const noexcept;

    // Convenience: sets the delta to deltaSeconds AND accumulates it into the
    // elapsed time. Negative deltas are asserted in debug like
    // setDeltaSeconds.
    void advance(double deltaSeconds) noexcept;

private:
    double m_deltaSeconds{0.0};
    double m_elapsedSeconds{0.0};
};

} // namespace infinity::core

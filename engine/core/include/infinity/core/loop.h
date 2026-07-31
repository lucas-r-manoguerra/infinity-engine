// infinity/core/loop.h
//
// Fixed-timestep engine loop (F2.5 part 2, ADR-006, rule 08/11). Update logic
// runs at exactly the fixed rate (60 Hz engine standard, defaultFixedDelta),
// while render/consume runs at the real frame rate; the two are decoupled by
// an accumulator - the classic "Fix Your Timestep" pattern. Consequence:
// deterministic simulation, the same world at any framerate (ADR-006).
//
// Determinism (rule 11): step() is a pure function of its inputs and state.
// The loop NEVER reads wall time - the caller measures real deltas with Clock
// and feeds them in. Identical sequences of step() calls therefore produce
// identical sequences of fixed updates, so tests drive the loop with
// synthetic deltas (no sleeps). No hidden state: everything lives in this
// explicit object. No allocation in the step path (rule 08): the update
// callback is a function pointer, not std::function.
#pragma once

#include "infinity/core/time.h"

namespace infinity::core {

// Fixed-timestep loop. Owns a Time instance and an accumulator; each step()
// consumes whole fixed steps and runs the update callback once per step,
// leaving the fractional remainder for render interpolation (alpha()).
class Loop {
public:
    // Update callback: receives the loop's Time, advanced by exactly one
    // fixed step (deltaSeconds() == fixedDeltaSeconds(), elapsedSeconds()
    // includes this step's advance). Function pointer so the step path never
    // allocates (rule 08).
    using Callback = void (*)(Time& time) noexcept;

    // Engine-standard fixed timestep: 60 Hz (ADR-006).
    [[nodiscard]] static constexpr double defaultFixedDelta() noexcept { return 1.0 / 60.0; }

    // Deleted: a fixed timestep must be chosen explicitly; a default of zero
    // would make every step() divide by it (rule 07).
    Loop() = delete;

    // Creates the loop with a fixed timestep. fixedDeltaSeconds must be > 0
    // (asserted in debug, ADR-003); the engine standard is defaultFixedDelta().
    // The loop is not copyable or movable: it owns simulation state (rule 03).
    explicit Loop(double fixedDeltaSeconds) noexcept;

    // Deleted copies/moves: ownership of the simulation state is explicit.
    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;
    Loop(Loop&&) noexcept = delete;
    Loop& operator=(Loop&&) noexcept = delete;

    // Sets the update callback, run once per fixed step. nullptr is a
    // programming error (asserted in debug, ADR-003); in release the loop
    // runs the steps and advances Time without calling back.
    void setUpdateCallback(Callback callback) noexcept;

    // Advances the loop by realDeltaSeconds of real time and runs the fixed
    // updates owed: integer division, whole steps only, each step advancing
    // Time by fixedDeltaSeconds BEFORE the callback runs. Pure math on inputs
    // and state - no wall time, no randomness, no allocation (rules 08, 11).
    // A negative delta is a programming error (asserted in debug, clamped to
    // zero in release, ADR-003); the delta is clamped to maxFrameDelta() so a
    // single huge frame (debugger pause) can never spiral into unbounded
    // catch-up updates.
    void step(double realDeltaSeconds) noexcept;

    // Sets the per-frame clamp (seconds): real deltas larger than this are
    // capped before accumulation, bounding the catch-up work to
    // ceil(maxFrameDelta / fixedDelta) updates per step (~15 at 60 Hz with
    // the default). Must be > 0 (asserted in debug). Default 0.25 s.
    void setMaxFrameDelta(double maxRealDeltaSeconds) noexcept;

    // The current clamp: the largest real delta a single step() accepts.
    [[nodiscard]] double maxFrameDelta() const noexcept;

    // Zeroes the accumulator and engine time: a clean restart (the callback
    // and clamp are kept).
    void reset() noexcept;

    // The fixed timestep (seconds) this loop was constructed with.
    [[nodiscard]] double fixedDeltaSeconds() const noexcept;

    // The loop's engine time, advanced by exactly fixedDeltaSeconds per
    // update. Mutable access is for consumers that legitimately read the
    // authoritative state (e.g. render interpolation); mutating it breaks the
    // monotonic-time invariant (ADR-006) and is the caller's responsibility.
    [[nodiscard]] Time& time() noexcept;
    [[nodiscard]] const Time& time() const noexcept;

    // Real time accumulated but not yet consumed by a fixed update: the
    // remainder available for render interpolation.
    [[nodiscard]] double accumulator() const noexcept;

    // Render-interpolation factor in [0, 1): accumulator / fixedDelta.
    // 0.0 when the fixed delta is 0 (impossible: the constructor requires
    // > 0, asserted in debug; never inf/NaN, rule 07).
    [[nodiscard]] double alpha() const noexcept;

private:
    double m_fixedDeltaSeconds{0.0};
    double m_maxFrameDelta{0.25};
    double m_accumulator{0.0};
    Time m_time;
    Callback m_updateCallback{nullptr};
};

} // namespace infinity::core

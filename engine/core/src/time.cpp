// src/time.cpp
#include "infinity/core/time.h"

#include <cassert>
#include <chrono>

namespace infinity::core {

namespace {

// steady_clock is guaranteed monotonic (rule 11): elapsed time measured from
// it never goes backwards, regardless of wall-clock adjustments.
using SteadyClock = std::chrono::steady_clock;

// Seconds between epoch and now. duration<double> converts in double, so a
// sub-microsecond interval never rounds to zero and precision is kept in the
// double result.
[[nodiscard]] double secondsSince(SteadyClock::time_point epoch) noexcept {
    const auto now = SteadyClock::now();
    return std::chrono::duration<double>(now - epoch).count();
}

} // namespace

Clock::Clock() noexcept : m_epoch(SteadyClock::now()) {}

double Clock::elapsedSeconds() const noexcept { return secondsSince(m_epoch); }

double Clock::elapsedMilliseconds() const noexcept { return secondsSince(m_epoch) * 1000.0; }

void Clock::reset() noexcept { m_epoch = SteadyClock::now(); }

void Time::setDeltaSeconds(double deltaSeconds) noexcept {
    assert(deltaSeconds >= 0.0); // a negative delta: time cannot flow backwards (ADR-006)
    m_deltaSeconds = deltaSeconds;
}

double Time::deltaSeconds() const noexcept { return m_deltaSeconds; }

void Time::setElapsedSeconds(double elapsedSeconds) noexcept {
    assert(elapsedSeconds >= m_elapsedSeconds); // backwards elapsed: the loop must be monotonic
    m_elapsedSeconds = elapsedSeconds;
}

double Time::elapsedSeconds() const noexcept { return m_elapsedSeconds; }

double Time::fps() const noexcept {
    if (m_deltaSeconds > 0.0) {
        return 1.0 / m_deltaSeconds;
    }
    return 0.0; // a non-positive delta yields 0, never inf/NaN (rule 07)
}

void Time::advance(double deltaSeconds) noexcept {
    assert(deltaSeconds >= 0.0);
    m_deltaSeconds = deltaSeconds;
    m_elapsedSeconds += deltaSeconds;
}

} // namespace infinity::core

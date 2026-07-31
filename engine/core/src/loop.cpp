// src/loop.cpp
#include "infinity/core/loop.h"

#include <cassert>

namespace infinity::core {

Loop::Loop(double fixedDeltaSeconds) noexcept : m_fixedDeltaSeconds(fixedDeltaSeconds) {
    // A non-positive fixed timestep is a programming error (ADR-003).
    assert(fixedDeltaSeconds > 0.0);
}

void Loop::setUpdateCallback(Callback callback) noexcept {
    assert(callback != nullptr); // a nullptr callback is a programming error (ADR-003)
    m_updateCallback = callback;
}

void Loop::step(double realDeltaSeconds) noexcept {
    assert(realDeltaSeconds >= 0.0); // negative real delta: time cannot flow backwards (ADR-006)

    // Release builds drop the assert; clamp to zero rather than rewind (ADR-003).
    if (realDeltaSeconds < 0.0) {
        realDeltaSeconds = 0.0;
    }

    // Defensive: the constructor asserts a positive timestep, but a
    // release-built Loop with a zero delta would spin forever below. A no-op
    // is the safest best-effort release behavior (rule 04).
    if (m_fixedDeltaSeconds <= 0.0) {
        return;
    }

    m_accumulator += realDeltaSeconds;
    if (m_accumulator > m_maxFrameDelta) {
        m_accumulator = m_maxFrameDelta; // spiral-of-death clamp: bound catch-up work
    }

    while (m_accumulator >= m_fixedDeltaSeconds) {
        m_accumulator -= m_fixedDeltaSeconds;
        m_time.advance(m_fixedDeltaSeconds);
        if (m_updateCallback != nullptr) {
            m_updateCallback(m_time);
        }
    }
}

void Loop::setMaxFrameDelta(double maxRealDeltaSeconds) noexcept {
    assert(maxRealDeltaSeconds > 0.0); // a non-positive clamp would stall all updates (ADR-003)
    m_maxFrameDelta = maxRealDeltaSeconds;
}

double Loop::maxFrameDelta() const noexcept { return m_maxFrameDelta; }

void Loop::reset() noexcept {
    m_accumulator = 0.0;
    m_time = Time{};
}

double Loop::fixedDeltaSeconds() const noexcept { return m_fixedDeltaSeconds; }

Time& Loop::time() noexcept { return m_time; }

const Time& Loop::time() const noexcept { return m_time; }

double Loop::accumulator() const noexcept { return m_accumulator; }

double Loop::alpha() const noexcept {
    if (m_fixedDeltaSeconds > 0.0) {
        return m_accumulator / m_fixedDeltaSeconds;
    }
    return 0.0; // non-positive timestep (programming error, asserted in debug): never inf/NaN
}

} // namespace infinity::core

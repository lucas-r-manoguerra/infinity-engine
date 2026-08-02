// src/profiler.cpp
#include "infinity/core/profiler.h"

#include <cassert>
#include <chrono>
#include <utility>

namespace infinity::core {

namespace {

// True when id names a live span (rule 04): an id at or beyond COUNT is a
// caller bug - a programming invariant, asserted in debug like every core
// invariant. Release keeps the raw array access; spans are hot-path and
// bounds-checking is not (rule 08).
// Release (NDEBUG) strips the asserting callers, so the helper is only used in
// debug builds; keep it declared to satisfy -Werror in both configurations.
[[nodiscard]] [[maybe_unused]] constexpr bool isValidSpanId(SpanId id) noexcept {
    return std::to_underlying(id) < SPAN_COUNT;
}

} // namespace

uint64_t steadyClockNs() noexcept {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

Profiler::Profiler(TimeNsFn timeNs) noexcept : m_timeNs(timeNs) {}

void Profiler::beginFrame() noexcept {
    m_frameSpanCount = 0;
    m_overflowed = false;
}

void Profiler::begin(SpanId id) noexcept {
    assert(isValidSpanId(id));
    assert(m_activeDepth < MAX_SPAN_DEPTH); // nesting past MAX_SPAN_DEPTH: caller bug (ADR-003)
    m_activeSpans[m_activeDepth] = ActiveSpan{.id = id, .startNs = m_timeNs()};
    ++m_activeDepth;
}

void Profiler::end([[maybe_unused]] SpanId id) noexcept {
    assert(isValidSpanId(id));
    assert(m_activeDepth > 0); // end() on an empty stack: caller bug (ADR-003)
    assert(m_activeSpans[m_activeDepth - 1].id == id); // LIFO mismatch: caller bug (ADR-003)
    --m_activeDepth;
    const ActiveSpan& span = m_activeSpans[m_activeDepth];
    const uint64_t nowNs = m_timeNs();
    if (m_frameSpanCount < MAX_FRAME_SPANS) {
        // A non-monotonic time source (now < start) clamps to 0, never wraps
        // (rule 07 policy): the monotonic contract is documented in the header.
        const uint64_t durationNs = nowNs >= span.startNs ? nowNs - span.startNs : 0;
        m_capture[m_frameSpanCount] = FrameSpan{.id = span.id,
                                                .depth = static_cast<uint16_t>(m_activeDepth),
                                                .startNs = span.startNs,
                                                .durationNs = durationNs};
        ++m_frameSpanCount;
    } else {
        // Capture full: drop the span, flag the overflow. A dropped span never
        // corrupts already-recorded data (see the header brief).
        m_overflowed = true;
    }
}

uint32_t Profiler::frameSpanCount() const noexcept { return m_frameSpanCount; }

std::span<const FrameSpan> Profiler::frameSpans() const noexcept {
    return {m_capture.data(), static_cast<size_t>(m_frameSpanCount)};
}

bool Profiler::wasOverflowed() const noexcept { return m_overflowed; }

Profiler& Profiler::instance() noexcept {
    static Profiler profiler;
    return profiler;
}

} // namespace infinity::core

// infinity/core/profiler.h
//
// Hierarchical frame profiler (F2.13, ADR-036, rules 03/08/11). Spans form a
// LIFO hierarchy rooted at a frame; each completed span records its nesting
// depth, start and duration into a fixed frame capture drained by the future
// renderer, debug UI (ADR-035) and logger (ADR-046). Instrumentation runs in
// the frame with ZERO allocations: everything lives in fixed arrays inside the
// Profiler object, so begin/end/frameSpans never allocate and never fail
// (rules 03/08).
//
//   Catalog      - SpanId is the FIXED, compile-time catalog of instrumentable
//                  spans (the same append-only discipline as CounterId and
//                  BudgetId: stable ids, never renumbered, never removed). The
//                  seed set covers the subsystems named by ADR-036; new
//                  modules append their spans at the end, before COUNT.
//   Hierarchy    - begin(id) pushes onto a LIFO stack; end(id) pops ONLY the
//                  current top. An end whose id does not match the top, an end
//                  on an empty stack and a begin past MAX_SPAN_DEPTH are
//                  programming errors: broken invariants, asserted in debug
//                  (ADR-003). Release keeps the raw array access like every
//                  core hot path (the diagnostics.h pattern).
//   Capture      - Written at end() in CLOSE order. Each FrameSpan records the
//                  id, the nesting depth after the pop, the start read from
//                  the time source at begin(), and durationNs = now - start. A
//                  non-monotonic time source (a contract violation) clamps
//                  durationNs to 0, never wraps (rule 07 policy).
//   Overflow     - The capture is a fixed MAX_FRAME_SPANS buffer. When full,
//                  the span is NOT recorded and the overflow flag is set; a
//                  dropped span never corrupts already-recorded data.
//                  wasOverflowed() stays true until the next beginFrame().
//   beginFrame() - Resets the capture (count = 0, overflow = false). The loop
//                  calls it once per frame; consumers drain via frameSpans()
//                  whenever they want after that.
//   Time source  - Injected in the constructor so tests use a deterministic
//                  fake (rule 11 / ADR-056); the default is steadyClockNs,
//                  backed by steady_clock (monotonic by construction, rule 11).
//   Thread-safety- NOT thread-safe by design: the profiler is used from the
//                  single frame thread. Multi-thread span capture is future
//                  work (documented, not a silent gap).
//   Ownership   - The Profiler is a plain object owned by the runtime,
//                  injected by reference where spans are recorded (rule 11:
//                  state lives in explicit objects, never in a global
//                  singleton).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace infinity::core {

// The FIXED, compile-time catalog of instrumentable spans (F2.13). Each
// enumerator is one span; COUNT is the catalog size, not a span. SpanId values
// are stable: they feed telemetry and debug UI, so existing ids never change
// meaning and new spans are appended only, before COUNT (see the header
// brief). The seed set covers the subsystems named by ADR-036.
// NOLINTNEXTLINE(performance-enum-size) -- deliberate: F2.13 spec pins the base to uint16_t.
enum class SpanId : uint16_t {
    FRAME = 0,      ///< root span of a frame
    FIXED_UPDATE,   ///< fixed-timestep update (ADR-006)
    ECS_UPDATE,     ///< ECS system updates (ADR-007)
    RENDERER_FRAME, ///< renderer frame (ADR-004)
    AI_UPDATE,      ///< AI systems (ContextSnapshot)
    PHYSICS_UPDATE, ///< physics (future phase)
    COUNT,          ///< sentinel - catalog size, not a span
};

// Number of live spans (the catalog size), derived from the COUNT sentinel so
// adding a span grows the capture storage automatically. Stable ids and
// append-only growth mean this value never decreases.
inline constexpr size_t SPAN_COUNT = std::to_underlying(SpanId::COUNT);

// Max nesting depth. A begin() past this is a programming error (ADR-003).
inline constexpr size_t MAX_SPAN_DEPTH = 16;

// Fixed capture capacity per frame. See the header brief for the overflow
// policy when a frame exceeds this many completed spans.
inline constexpr size_t MAX_FRAME_SPANS = 256;

// Human-readable names of the span catalog, one per SpanId < SPAN_COUNT.
// Kept as an inline constexpr so spanName() is a compile-time lookup
// (rule 02). "COUNT" is deliberately not a name: it is a sentinel, not a span.
inline constexpr std::array<std::string_view, SPAN_COUNT> SPAN_NAMES{
    "FRAME", "FIXED_UPDATE", "ECS_UPDATE", "RENDERER_FRAME", "AI_UPDATE", "PHYSICS_UPDATE"};

// One completed span in the frame capture, written at end().
struct FrameSpan {
    SpanId id;
    uint16_t depth;      // nesting level at close (0 = root)
    uint64_t startNs;    // from the time source
    uint64_t durationNs; // clamped to 0 if the time source is non-monotonic
};

// Time source: returns monotonic nanoseconds. Injected in the constructor so
// tests use a deterministic fake (rule 11 / ADR-056); the default is
// steadyClockNs (steady_clock, monotonic by construction).
using TimeNsFn = uint64_t (*)() noexcept;

// Returns steady_clock now, in nanoseconds since its epoch (monotonic by
// construction, rule 11). The default Profiler time source.
[[nodiscard]] uint64_t steadyClockNs() noexcept;

// Hierarchical frame profiler (F2.13, ADR-036). See the header brief for the
// catalog, hierarchy, capture, overflow, time source, thread-safety and
// rule-11 tradeoff.
class Profiler {
public:
    // A fresh instance has an empty capture and no active spans. timeNs
    // defaults to the steady_clock source; tests inject a deterministic fake.
    explicit Profiler(TimeNsFn timeNs = steadyClockNs) noexcept;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;
    Profiler(Profiler&&) = delete;
    Profiler& operator=(Profiler&&) = delete;

    // Resets the frame capture: count to 0 and the overflow flag to false.
    // The loop calls it once per frame.
    void beginFrame() noexcept;

    // Pushes span id onto the active stack, reading its start from the time
    // source. id must name a catalog span and the stack must not already hold
    // MAX_SPAN_DEPTH spans (programming errors, asserted in debug, ADR-003).
    void begin(SpanId id) noexcept;

    // Pops the current top, expecting it to be id, and records the completed
    // FrameSpan into the capture. An end() on an empty stack or with an id
    // that does not match the top is a programming error (asserted in debug,
    // ADR-003). A full capture drops the span and sets the overflow flag
    // instead (see the header brief).
    void end(SpanId id) noexcept;

    // Spans recorded since the last beginFrame().
    [[nodiscard]] uint32_t frameSpanCount() const noexcept;

    // The frame capture in CLOSE order, sized by frameSpanCount(). The view is
    // non-owning and valid until the next beginFrame().
    [[nodiscard]] std::span<const FrameSpan> frameSpans() const noexcept;

    // True when the capture filled up: later spans were dropped until the next
    // beginFrame().
    [[nodiscard]] bool wasOverflowed() const noexcept;

    // Stable name of span id (compile-time lookup over SPAN_NAMES).
    [[nodiscard]] static constexpr std::string_view spanName(SpanId id) noexcept {
        return SPAN_NAMES[static_cast<size_t>(id)];
    }

private:
    struct ActiveSpan {
        SpanId id;
        uint64_t startNs;
    };

    TimeNsFn m_timeNs{steadyClockNs};
    std::array<ActiveSpan, MAX_SPAN_DEPTH> m_activeSpans{};
    size_t m_activeDepth{0};
    std::array<FrameSpan, MAX_FRAME_SPANS> m_capture{};
    uint32_t m_frameSpanCount{0};
    bool m_overflowed{false};
};

} // namespace infinity::core

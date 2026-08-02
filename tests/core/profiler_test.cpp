// tests/core/profiler_test.cpp
//
// Profiler contract tests (F2.13, ADR-036, rules 06/08/11): the frame capture
// records root and nested spans with their depth, close order, start and
// duration; beginFrame resets the capture and the overflow flag; nesting up to
// MAX_SPAN_DEPTH works; a non-monotonic time source clamps the duration to 0
// instead of wrapping (rule 07); a full capture drops later spans without
// corrupting already-recorded ones; frameSpans exposes the capture in close
// order; spanName names every catalog id below COUNT; and a local instance
// keeps its capture fully isolated (rule 11: no global state).
//
// Time source (rule 11): TimeNsFn is a raw function pointer, so the fake that
// drives it is file-scope TEST scaffolding (the budget_allocator_test
// g_recorder pattern) - the profiler itself has no hidden state. The queue
// lives here, never in engine code; every TEST_CASE resets it before running.
// The fake pops one value per timeNs() call: begin() reads the span's start,
// end() reads its end, so a test pushes exactly the values its calls read.
#include "infinity/core/profiler.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <string_view>

#include <doctest/doctest.h>

namespace {

using infinity::core::FrameSpan;
using infinity::core::MAX_FRAME_SPANS;
using infinity::core::MAX_SPAN_DEPTH;
using infinity::core::Profiler;
using infinity::core::SPAN_COUNT;
using infinity::core::SpanId;

// Largest value a test pushes: the overflow case pushes MAX_FRAME_SPANS + N
// begin/end pairs, two reads per pair, plus slack.
constexpr size_t FAKE_CAPACITY = (MAX_FRAME_SPANS * 2) + 64;

// Deterministic time-source backing (rule 11 test scaffolding, see the file
// brief). Reset at the start of every TEST_CASE so no case leaks queue
// position into another.
struct FakeClockState {
    std::array<uint64_t, FAKE_CAPACITY> values{};
    size_t count{0};
    size_t read{0};
};

FakeClockState g_fakeClock;

// Re-arms the fake for a fresh TEST_CASE.
void resetClock() noexcept { g_fakeClock = FakeClockState{}; }

// Queues one value; the next fakeNow() call returns it (FIFO). Pushing exactly
// the values the test's begin/end calls read keeps every case deterministic
// (rule 11).
void pushNow(uint64_t ns) noexcept {
    g_fakeClock.values[g_fakeClock.count] = ns;
    ++g_fakeClock.count;
}

// The TimeNsFn handed to local Profilers. One queue pop per call.
uint64_t fakeNow() noexcept {
    const uint64_t value = g_fakeClock.values[g_fakeClock.read];
    ++g_fakeClock.read;
    return value;
}

// Isolated from CHECK so doctest never has to decompile a SpanId operand
// (the error_test.cpp pattern).
[[nodiscard]] bool isSpan(const FrameSpan& span, SpanId id) noexcept { return span.id == id; }

} // namespace

TEST_CASE("Root span records start and duration from the time source") {
    resetClock();
    pushNow(1000);
    pushNow(1500);
    Profiler profiler{fakeNow};
    profiler.beginFrame();
    profiler.begin(SpanId::FRAME);
    profiler.end(SpanId::FRAME);

    CHECK(profiler.frameSpanCount() == 1);
    const std::span<const FrameSpan> spans = profiler.frameSpans();
    CHECK(spans.size() == 1);
    CHECK(isSpan(spans[0], SpanId::FRAME));
    CHECK(spans[0].depth == 0);
    CHECK(spans[0].startNs == 1000);
    CHECK(spans[0].durationNs == 500);
}

TEST_CASE("Nested spans record depth and close order") {
    resetClock();
    Profiler profiler{fakeNow};
    profiler.beginFrame();

    pushNow(1000);
    profiler.begin(SpanId::FRAME);
    pushNow(1050);
    profiler.begin(SpanId::ECS_UPDATE);
    pushNow(1100);
    profiler.begin(SpanId::RENDERER_FRAME);
    pushNow(1150);
    profiler.end(SpanId::RENDERER_FRAME);
    pushNow(1200);
    profiler.end(SpanId::ECS_UPDATE);
    pushNow(1300);
    profiler.end(SpanId::FRAME);

    CHECK(profiler.frameSpanCount() == 3);
    const std::span<const FrameSpan> spans = profiler.frameSpans();
    CHECK(spans.size() == 3);

    // The innermost span closes first: capture is in close order (depth 2).
    CHECK(isSpan(spans[0], SpanId::RENDERER_FRAME));
    CHECK(spans[0].depth == 2);
    CHECK(spans[0].startNs == 1100);
    CHECK(spans[0].durationNs == 50);

    CHECK(isSpan(spans[1], SpanId::ECS_UPDATE));
    CHECK(spans[1].depth == 1);
    CHECK(spans[1].startNs == 1050);
    CHECK(spans[1].durationNs == 150);

    // The root closes last: depth 0.
    CHECK(isSpan(spans[2], SpanId::FRAME));
    CHECK(spans[2].depth == 0);
    CHECK(spans[2].startNs == 1000);
    CHECK(spans[2].durationNs == 300);
}

TEST_CASE("beginFrame resets capture count and overflow flag") {
    resetClock();
    Profiler profiler{fakeNow};
    profiler.beginFrame();

    // Emit more spans than the capture can hold to trip the overflow flag.
    for (size_t i = 0; i < MAX_FRAME_SPANS + 1; ++i) {
        pushNow(i * 2);
        profiler.begin(SpanId::ECS_UPDATE);
        pushNow((i * 2) + 1);
        profiler.end(SpanId::ECS_UPDATE);
    }
    CHECK(profiler.wasOverflowed());
    CHECK(profiler.frameSpanCount() == MAX_FRAME_SPANS);

    profiler.beginFrame();
    CHECK(profiler.frameSpanCount() == 0);
    CHECK_FALSE(profiler.wasOverflowed());
}

TEST_CASE("Unlimited nesting within depth") {
    resetClock();
    Profiler profiler{fakeNow};
    profiler.beginFrame();

    // MAX_SPAN_DEPTH nested spans (the stack's exact capacity). Reusing one
    // id is fine: LIFO matching is positional, not by identity.
    for (size_t i = 0; i < MAX_SPAN_DEPTH; ++i) {
        pushNow(i * 2);
        profiler.begin(SpanId::ECS_UPDATE);
    }
    for (size_t i = MAX_SPAN_DEPTH; i > 0; --i) {
        const size_t level = i - 1;
        pushNow((level * 2) + 1);
        profiler.end(SpanId::ECS_UPDATE);
    }

    CHECK(profiler.frameSpanCount() == MAX_SPAN_DEPTH);
    const std::span<const FrameSpan> spans = profiler.frameSpans();
    for (size_t i = 0; i < MAX_SPAN_DEPTH; ++i) {
        CAPTURE(i);
        CHECK(spans[i].depth == MAX_SPAN_DEPTH - 1 - i);
        CHECK(spans[i].durationNs == 1);
    }
}

TEST_CASE("Duration clamps to zero when time source is non-monotonic") {
    resetClock();
    pushNow(1000);
    pushNow(500);
    Profiler profiler{fakeNow};
    profiler.beginFrame();
    profiler.begin(SpanId::AI_UPDATE);
    profiler.end(SpanId::AI_UPDATE);

    const std::span<const FrameSpan> spans = profiler.frameSpans();
    CHECK(spans.size() == 1);
    CHECK(isSpan(spans[0], SpanId::AI_UPDATE));
    CHECK(spans[0].startNs == 1000);
    CHECK(spans[0].durationNs == 0);
}

TEST_CASE("Capture overflow drops spans and sets the flag") {
    resetClock();
    Profiler profiler{fakeNow};
    profiler.beginFrame();

    constexpr size_t EXTRA_SPANS = 10;
    for (size_t i = 0; i < MAX_FRAME_SPANS + EXTRA_SPANS; ++i) {
        pushNow(i * 2);
        profiler.begin(SpanId::ECS_UPDATE);
        pushNow((i * 2) + 1);
        profiler.end(SpanId::ECS_UPDATE);
    }

    CHECK(profiler.frameSpanCount() == MAX_FRAME_SPANS);
    CHECK(profiler.wasOverflowed());

    // The first recorded span is still exact: a dropped span never corrupts
    // already-recorded data.
    const std::span<const FrameSpan> spans = profiler.frameSpans();
    CHECK(spans.size() == MAX_FRAME_SPANS);
    CHECK(isSpan(spans[0], SpanId::ECS_UPDATE));
    CHECK(spans[0].depth == 0);
    CHECK(spans[0].startNs == 0);
    CHECK(spans[0].durationNs == 1);
}

TEST_CASE("frameSpans view exposes recorded spans in order") {
    resetClock();
    Profiler profiler{fakeNow};
    profiler.beginFrame();

    pushNow(0);
    profiler.begin(SpanId::FRAME);
    pushNow(10);
    profiler.end(SpanId::FRAME);
    pushNow(20);
    profiler.begin(SpanId::FIXED_UPDATE);
    pushNow(25);
    profiler.end(SpanId::FIXED_UPDATE);
    pushNow(30);
    profiler.begin(SpanId::RENDERER_FRAME);
    pushNow(40);
    profiler.end(SpanId::RENDERER_FRAME);

    const std::span<const FrameSpan> spans = profiler.frameSpans();
    CHECK(spans.size() == 3);

    CHECK(isSpan(spans[0], SpanId::FRAME));
    CHECK(spans[0].depth == 0);
    CHECK(spans[0].startNs == 0);
    CHECK(spans[0].durationNs == 10);

    CHECK(isSpan(spans[1], SpanId::FIXED_UPDATE));
    CHECK(spans[1].depth == 0);
    CHECK(spans[1].startNs == 20);
    CHECK(spans[1].durationNs == 5);

    CHECK(isSpan(spans[2], SpanId::RENDERER_FRAME));
    CHECK(spans[2].depth == 0);
    CHECK(spans[2].startNs == 30);
    CHECK(spans[2].durationNs == 10);
}

TEST_CASE("spanName covers every id below count") {
    constexpr std::array<std::string_view, SPAN_COUNT> EXPECTED_NAMES{
        "FRAME", "FIXED_UPDATE", "ECS_UPDATE", "RENDERER_FRAME", "AI_UPDATE", "PHYSICS_UPDATE"};

    for (size_t i = 0; i < SPAN_COUNT; ++i) {
        const std::string_view name = Profiler::spanName(static_cast<SpanId>(i));
        CAPTURE(i);
        CHECK_FALSE(name.empty());
        CHECK(name == EXPECTED_NAMES[i]);
    }

    for (size_t i = 0; i < SPAN_COUNT; ++i) {
        const std::string_view name = Profiler::spanName(static_cast<SpanId>(i));
        for (size_t j = i + 1; j < SPAN_COUNT; ++j) {
            CHECK(name != Profiler::spanName(static_cast<SpanId>(j)));
        }
    }
}

TEST_CASE("local profiler keeps its capture fully isolated") {
    resetClock();
    pushNow(100);
    pushNow(150);
    Profiler local{fakeNow};
    local.beginFrame();
    local.begin(SpanId::FRAME);
    local.end(SpanId::FRAME);

    CHECK(local.frameSpanCount() == 1);

    // A second instance never sees the first one's capture (rule 11: state
    // lives in explicit objects, never in a global singleton).
    Profiler other{fakeNow};
    other.beginFrame();
    CHECK(other.frameSpanCount() == 0);
    CHECK_FALSE(other.wasOverflowed());
}

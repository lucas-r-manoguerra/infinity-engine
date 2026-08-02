// tests/core/time_budgets_alert_test.cpp
//
// TimeBudgets ALERT EDGE contract tests (F2.14, ADR-055, rules 04/06/08/11):
// a full capture (overflow) alerts only for the recorded spans - every
// recorded span alerts with its exact values in capture order and the dropped
// spans produce no extra alerts - and setAlert(nullptr) silences the callback
// while the exceeded count is still returned. The declaration cases and the
// within-budget/order verification cases live in
// time_budgets_declaration_test.cpp and time_budgets_check_test.cpp (rule 01:
// One File = One Task).
//
// Time source (rule 11): TimeNsFn is a raw function pointer, so the fake that
// drives it is file-scope TEST scaffolding (the profiler_test g_fakeClock
// pattern) - the tracker itself has no hidden state. The queue lives here,
// never in engine code; every TEST_CASE resets it before running. The fake
// pops one value per timeNs() call: begin() reads the span's start, end()
// reads its end, so a test pushes exactly the values its calls read. The
// alert callback is recorded the same way: file-scope scaffolding, reset per
// TEST_CASE.
#include "infinity/core/profiler.h"
#include "infinity/core/time_budgets.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>

#include <doctest/doctest.h>

namespace {

using infinity::core::MAX_FRAME_SPANS;
using infinity::core::Profiler;
using infinity::core::SpanId;
using infinity::core::TimeBudgets;

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

// One captured alert callback invocation.
struct AlertRecord {
    SpanId id;
    uint64_t durationNs;
    uint64_t budgetNs;
};

// Largest number of alerts a test collects: the overflow case fires one alert
// per recorded span, MAX_FRAME_SPANS of them, plus slack.
constexpr size_t ALERT_CAPACITY = MAX_FRAME_SPANS + 32;

// Deterministic alert-callback backing (rule 11 test scaffolding). Reset at
// the start of every TEST_CASE that asserts on alerts.
struct AlertState {
    std::array<AlertRecord, ALERT_CAPACITY> records{};
    size_t count{0};
};

AlertState g_alerts;

// Re-arms the alert recorder for a fresh TEST_CASE.
void resetAlerts() noexcept { g_alerts = AlertState{}; }

// The BudgetExceededFn handed to local TimeBudgets instances.
void recordAlert(SpanId id, uint64_t durationNs, uint64_t budgetNs) noexcept {
    if (g_alerts.count < ALERT_CAPACITY) {
        g_alerts.records[g_alerts.count] =
            AlertRecord{.id = id, .durationNs = durationNs, .budgetNs = budgetNs};
        ++g_alerts.count;
    }
}

// Isolated from CHECK so doctest never has to decompile a SpanId operand
// (the profiler_test pattern).
[[nodiscard]] bool isAlertFor(const AlertRecord& alert, SpanId id) noexcept {
    return alert.id == id;
}

} // namespace

TEST_CASE("full capture alerts only for the recorded spans") {
    resetClock();
    resetAlerts();
    TimeBudgets budgets;
    CHECK(budgets.setBudget(SpanId::ECS_UPDATE, 1).has_value());
    budgets.setAlert(recordAlert);

    Profiler profiler{fakeNow};
    profiler.beginFrame();

    constexpr size_t EXTRA_SPANS = 10;
    for (size_t i = 0; i < MAX_FRAME_SPANS + EXTRA_SPANS; ++i) {
        pushNow(i * 2);
        profiler.begin(SpanId::ECS_UPDATE);
        pushNow((i * 2) + 2);
        profiler.end(SpanId::ECS_UPDATE); // duration 2 > 1
    }

    CHECK(profiler.wasOverflowed());
    CHECK(profiler.frameSpanCount() == MAX_FRAME_SPANS);
    CHECK(budgets.checkFrame(profiler) == MAX_FRAME_SPANS);
    CHECK(g_alerts.count == MAX_FRAME_SPANS);

    // Every recorded span alerted with its exact values, in capture order; the
    // dropped spans produced no extra alerts.
    for (size_t i = 0; i < MAX_FRAME_SPANS; ++i) {
        CAPTURE(i);
        CHECK(isAlertFor(g_alerts.records[i], SpanId::ECS_UPDATE));
        CHECK(g_alerts.records[i].durationNs == 2);
        CHECK(g_alerts.records[i].budgetNs == 1);
    }
}

TEST_CASE("setAlert with nullptr silences the callback but keeps the count") {
    resetClock();
    resetAlerts();
    TimeBudgets budgets;
    CHECK(budgets.setBudget(SpanId::FRAME, 100).has_value());
    budgets.setAlert(recordAlert);

    Profiler profiler{fakeNow};
    profiler.beginFrame();
    pushNow(0);
    profiler.begin(SpanId::FRAME);
    pushNow(500);
    profiler.end(SpanId::FRAME); // duration 500 > 100

    CHECK(budgets.checkFrame(profiler) == 1);
    CHECK(g_alerts.count == 1);

    // Silence the alert; the tracker still reports the exceeded count.
    budgets.setAlert(nullptr);
    resetAlerts();
    profiler.beginFrame();
    pushNow(1000);
    profiler.begin(SpanId::FRAME);
    pushNow(1600);
    profiler.end(SpanId::FRAME); // duration 600 > 100

    CHECK(budgets.checkFrame(profiler) == 1);
    CHECK(g_alerts.count == 0);
}

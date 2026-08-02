// tests/core/time_budgets_check_test.cpp
//
// TimeBudgets VERIFICATION contract tests (F2.14, ADR-055, rules 04/06/08/11):
// checkFrame reports the count of spans that exceeded their declared budget
// and calls the alert callback once per exceeded span in capture order with
// the exact id/durationNs/budgetNs, duration == budget does not exceed, and a
// span without a declared budget never alerts. The declaration cases
// (setBudget, error branches, local isolation) and the overflow/alert-silencing
// cases live in time_budgets_declaration_test.cpp and
// time_budgets_alert_test.cpp (rule 01: One File = One Task).
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

// Largest value a test pushes: the overflow case (time_budgets_alert_test.cpp)
// pushes MAX_FRAME_SPANS + N begin/end pairs, two reads per pair, plus slack.
// Kept identical across the suite so every file matches the original.
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

TEST_CASE("checkFrame reports zero when every recorded span is within budget") {
    resetClock();
    TimeBudgets budgets;
    CHECK(budgets.setBudget(SpanId::FRAME, 1000).has_value());
    CHECK(budgets.setBudget(SpanId::ECS_UPDATE, 500).has_value());

    Profiler profiler{fakeNow};
    profiler.beginFrame();

    pushNow(1000);
    profiler.begin(SpanId::FRAME);
    pushNow(1500);
    profiler.end(SpanId::FRAME); // duration 500 <= 1000
    pushNow(2000);
    profiler.begin(SpanId::ECS_UPDATE);
    pushNow(2200);
    profiler.end(SpanId::ECS_UPDATE); // duration 200 <= 500

    CHECK(budgets.checkFrame(profiler) == 0);
}

TEST_CASE("checkFrame reports one exceeded span and alerts with exact values") {
    resetClock();
    resetAlerts();
    TimeBudgets budgets;
    CHECK(budgets.setBudget(SpanId::ECS_UPDATE, 100).has_value());
    budgets.setAlert(recordAlert);

    Profiler profiler{fakeNow};
    profiler.beginFrame();

    pushNow(1000);
    profiler.begin(SpanId::ECS_UPDATE);
    pushNow(1300);
    profiler.end(SpanId::ECS_UPDATE); // duration 300 > 100

    CHECK(budgets.checkFrame(profiler) == 1);
    CHECK(g_alerts.count == 1);
    CHECK(isAlertFor(g_alerts.records[0], SpanId::ECS_UPDATE));
    CHECK(g_alerts.records[0].durationNs == 300);
    CHECK(g_alerts.records[0].budgetNs == 100);
}

TEST_CASE("checkFrame reports every exceeded span in capture order") {
    resetClock();
    resetAlerts();
    TimeBudgets budgets;
    CHECK(budgets.setBudget(SpanId::FRAME, 400).has_value());
    CHECK(budgets.setBudget(SpanId::ECS_UPDATE, 100).has_value());
    CHECK(budgets.setBudget(SpanId::RENDERER_FRAME, 500).has_value());
    budgets.setAlert(recordAlert);

    Profiler profiler{fakeNow};
    profiler.beginFrame();

    pushNow(0);
    profiler.begin(SpanId::FRAME);
    pushNow(1000);
    profiler.end(SpanId::FRAME); // duration 1000 > 400 -> alert
    pushNow(2000);
    profiler.begin(SpanId::ECS_UPDATE);
    pushNow(2200);
    profiler.end(SpanId::ECS_UPDATE); // duration 200 > 100 -> alert
    pushNow(3000);
    profiler.begin(SpanId::RENDERER_FRAME);
    pushNow(3200);
    profiler.end(SpanId::RENDERER_FRAME); // duration 200 <= 500 -> within
    pushNow(4000);
    profiler.begin(SpanId::AI_UPDATE);
    pushNow(4500);
    profiler.end(SpanId::AI_UPDATE); // duration 500, no declared budget

    CHECK(budgets.checkFrame(profiler) == 2);
    CHECK(g_alerts.count == 2);
    CHECK(isAlertFor(g_alerts.records[0], SpanId::FRAME));
    CHECK(g_alerts.records[0].durationNs == 1000);
    CHECK(g_alerts.records[0].budgetNs == 400);
    CHECK(isAlertFor(g_alerts.records[1], SpanId::ECS_UPDATE));
    CHECK(g_alerts.records[1].durationNs == 200);
    CHECK(g_alerts.records[1].budgetNs == 100);
}

TEST_CASE("duration equal to budget does not exceed") {
    resetClock();
    resetAlerts();
    TimeBudgets budgets;
    CHECK(budgets.setBudget(SpanId::FRAME, 100).has_value());
    budgets.setAlert(recordAlert);

    Profiler profiler{fakeNow};
    profiler.beginFrame();

    pushNow(1000);
    profiler.begin(SpanId::FRAME);
    pushNow(1100);
    profiler.end(SpanId::FRAME); // duration 100 == budget 100

    CHECK(budgets.checkFrame(profiler) == 0);
    CHECK(g_alerts.count == 0);
}

TEST_CASE("checkFrame never alerts for spans without a declared budget") {
    resetClock();
    resetAlerts();
    TimeBudgets budgets;
    budgets.setAlert(recordAlert);

    Profiler profiler{fakeNow};
    profiler.beginFrame();

    pushNow(0);
    profiler.begin(SpanId::FRAME);
    pushNow(5000);
    profiler.end(SpanId::FRAME); // duration 5000, no declared budget

    CHECK(budgets.checkFrame(profiler) == 0);
    CHECK(g_alerts.count == 0);
}

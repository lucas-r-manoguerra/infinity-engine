// tests/core/time_budgets_declaration_test.cpp
//
// TimeBudgets DECLARATION contract tests (F2.14, ADR-055, rules 04/06/08/11):
// setBudget + budget round-trip, every declared error branch (invalid span id
// and duplicate declaration) is a recoverable error that changes nothing
// (rule 04), and a local instance keeps its budgets fully isolated (rule 11:
// no global state). The verification/alert cases live in
// time_budgets_check_test.cpp (checkFrame) and time_budgets_alert_test.cpp
// (overflow + alert silencing) - rule 01: One File = One Task.
//
// Time source (rule 11): TimeNsFn is a raw function pointer, so the fake that
// drives it is file-scope TEST scaffolding (the profiler_test g_fakeClock
// pattern) - the tracker itself has no hidden state. The declaration cases
// never read the clock (no span is captured), so only the reset half of the
// scaffolding is kept here; pushNow/fakeNow would be dead code under -Werror.
#include "infinity/core/profiler.h"
#include "infinity/core/time_budgets.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>

#include <doctest/doctest.h>

namespace {

using infinity::core::CoreError;
using infinity::core::ExpectedVoid;
using infinity::core::MAX_FRAME_SPANS;
using infinity::core::SpanId;
using infinity::core::TimeBudgets;

// Largest value a test pushes: kept for parity with the check/alert suites;
// declaration cases never fill the fake clock.
constexpr size_t FAKE_CAPACITY = (MAX_FRAME_SPANS * 2) + 64;

// Deterministic time-source backing (rule 11 test scaffolding, see the file
// brief). Reset at the start of every TEST_CASE so no case leaks queue
// position into another. Only the reset half is exercised here: declaration
// cases never call fakeNow.
struct FakeClockState {
    std::array<uint64_t, FAKE_CAPACITY> values{};
    size_t count{0};
    size_t read{0};
};

FakeClockState g_fakeClock;

// Re-arms the fake for a fresh TEST_CASE.
void resetClock() noexcept { g_fakeClock = FakeClockState{}; }

// Isolated from CHECK so doctest never has to decompile a CoreError operand
// (the error_test.cpp pattern).
bool isError(const ExpectedVoid& result, CoreError expected) noexcept {
    return !result.has_value() && result.error() == expected;
}

} // namespace

TEST_CASE("setBudget stores a catalog span budget and budget returns it") {
    resetClock();
    TimeBudgets budgets;

    CHECK(budgets.setBudget(SpanId::ECS_UPDATE, 1000).has_value());
    CHECK(budgets.budget(SpanId::ECS_UPDATE) == 1000);

    // An undeclared span queries as 0 (not declared).
    CHECK(budgets.budget(SpanId::FRAME) == 0);
    CHECK(budgets.budget(SpanId::PHYSICS_UPDATE) == 0);
}

TEST_CASE("invalid span id is a recoverable error and changes nothing") {
    resetClock();
    TimeBudgets budgets;

    CHECK(budgets.setBudget(SpanId::FRAME, 100).has_value());

    // NOLINTNEXTLINE(modernize-use-auto) -- rule 02: no auto for trivial types.
    const SpanId atCount = static_cast<SpanId>(SpanId::COUNT);
    CHECK(isError(budgets.setBudget(atCount, 50), CoreError::INVALID_ARGUMENT));
    CHECK(isError(budgets.setBudget(atCount, 60), CoreError::INVALID_ARGUMENT));

    // A far-out id proves the range check, not just the == COUNT check. No
    // auto for trivial types (rule 02); the cast is a deliberate out-of-range.
    // NOLINTNEXTLINE(modernize-use-auto, clang-analyzer-optin.core.EnumCastOutOfRange)
    const SpanId farOut = static_cast<SpanId>(9999);
    CHECK(isError(budgets.setBudget(farOut, 50), CoreError::INVALID_ARGUMENT));

    // The failed registrations changed nothing: the valid declaration still
    // holds and the catalog still has undeclared entries.
    CHECK(budgets.budget(SpanId::FRAME) == 100);
    CHECK(budgets.budget(SpanId::ECS_UPDATE) == 0);
}

TEST_CASE("duplicate declaration is a recoverable error and changes nothing") {
    resetClock();
    TimeBudgets budgets;

    CHECK(budgets.setBudget(SpanId::ECS_UPDATE, 100).has_value());
    CHECK(isError(budgets.setBudget(SpanId::ECS_UPDATE, 200), CoreError::DUPLICATE_BUDGET));

    // The first declaration still holds.
    CHECK(budgets.budget(SpanId::ECS_UPDATE) == 100);
}

TEST_CASE("local instance keeps its budgets fully isolated") {
    resetClock();
    TimeBudgets local;
    CHECK(local.setBudget(SpanId::ECS_UPDATE, 100).has_value());

    // A second instance never sees the first one's budgets (rule 11: state
    // lives in explicit objects, never in a global singleton).
    TimeBudgets other;
    CHECK(other.budget(SpanId::ECS_UPDATE) == 0);
    CHECK(other.budget(SpanId::FRAME) == 0);
}

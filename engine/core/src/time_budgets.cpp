// src/time_budgets.cpp
#include "infinity/core/time_budgets.h"

#include <cassert>
#include <expected>
#include <utility>

namespace infinity::core {

namespace {

// True when id names a catalog span (rule 04): an id at or beyond COUNT is a
// caller bug - a programming invariant, asserted in debug on the query path
// and rejected as a recoverable error by setBudget (rule 04).
[[nodiscard]] constexpr bool isValidSpanId(SpanId id) noexcept {
    return std::to_underlying(id) < SPAN_COUNT;
}

} // namespace

ExpectedVoid TimeBudgets::setBudget(SpanId id, uint64_t maxNs) noexcept {
    if (!isValidSpanId(id)) {
        return std::unexpected(CoreError::INVALID_ARGUMENT);
    }
    // NOLINTNEXTLINE(modernize-use-auto) -- rule 02: no auto for trivial types.
    const size_t index = static_cast<size_t>(id);
    if (m_budgetsNs[index] != 0) {
        return std::unexpected(CoreError::DUPLICATE_BUDGET);
    }
    m_budgetsNs[index] = maxNs;
    return {};
}

uint64_t TimeBudgets::budget(SpanId id) const noexcept {
    assert(isValidSpanId(id));
    return m_budgetsNs[static_cast<size_t>(id)];
}

void TimeBudgets::setAlert(BudgetExceededFn fn) noexcept { m_alert = fn; }

uint32_t TimeBudgets::checkFrame(const Profiler& profiler) noexcept {
    uint32_t exceeded = 0;
    for (const FrameSpan& span : profiler.frameSpans()) {
        const uint64_t maxNs = m_budgetsNs[static_cast<size_t>(span.id)];
        if (maxNs == 0) {
            continue; // no declared budget: never alerts
        }
        if (span.durationNs > maxNs) {
            ++exceeded;
            if (m_alert != nullptr) {
                m_alert(span.id, span.durationNs, maxNs);
            }
        }
    }
    return exceeded;
}

TimeBudgets& TimeBudgets::instance() noexcept {
    static TimeBudgets budgets;
    return budgets;
}

} // namespace infinity::core

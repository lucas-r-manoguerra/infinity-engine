// infinity/core/time_budgets.h
//
// Per-system time budget tracker (F2.14, ADR-055, rules 04/08/11). Each
// subsystem declares the max duration of its instrumented SpanId per frame;
// checkFrame() verifies the current frame capture of the Profiler (ADR-036)
// against those declarations and reports every span that exceeded its budget.
// The budget is part of each system's definition, not an afterthought: a frame
// is a contract between what a system is allowed to cost and what it actually
// cost, and exceeding it is a visible bug - an alert plus a diagnostics
// counter, never silence.
//
//   Catalog    - Reuses the SpanId catalog from the Profiler (profiler.h): a
//                budget is declared per span, and an id that names no catalog
//                span (>= SPAN_COUNT) is a recoverable error (rule 04).
//   Wiring     - setBudget() declares a span's max duration in ns.
//                Registration is init-time wiring, never a hot path
//                (rule 08): out-of-range ids and duplicate declarations are
//                recoverable errors (rule 04) that change nothing - a failed
//                registration is a no-op.
//   Verification - checkFrame() runs over the Profiler's frameSpans() capture
//                (CLOSE order) once per frame: O(MAX_FRAME_SPANS), zero
//                allocation (rule 08). A span whose durationNs > its declared
//                budget exceeds; durationNs == budget does NOT exceed; a span
//                with no declared budget (0) never alerts. The alert callback
//                fires once per exceeded span, in capture order, with the
//                exact id/durationNs/budgetNs.
//   Alerts     - The BudgetExceededFn callback is the wiring point for the
//                future logger / dev console (ADR-046/035): the tracker stays
//                decoupled and never bumps Diagnostics itself - the wiring
//                lives where the alert callback is consumed (F2.14, ADR-055).
//                setAlert(nullptr) disables the callback while checkFrame still
//                reports the exceeded count.
//   Thread-safety - NOT thread-safe by design: checkFrame runs on the same
//                frame thread as the Profiler it verifies (multi-thread span
//                capture is future work, documented in profiler.h).
//   Ownership  - The tracker is a plain object owned by the runtime, injected
//                by reference where budgets are consumed (rule 11: state lives
//                in explicit objects, never in a global singleton).
#pragma once

#include "infinity/core/error.h"
#include "infinity/core/profiler.h"

#include <array>
#include <cstdint>

namespace infinity::core {

// Alert callback: fired by checkFrame() once per span that exceeded its
// declared budget, with the span id and the exact observed duration and
// declared budget in ns. Registered via setAlert(); nullptr disables.
using BudgetExceededFn = void (*)(SpanId id, uint64_t durationNs, uint64_t budgetNs) noexcept;

// Per-system time budget tracker (F2.14, ADR-055). See the header brief for
// the catalog, wiring, verification, alerts, thread-safety and rule-11
// tradeoff.
class TimeBudgets {
public:
    // A fresh instance has no declared budgets (the array is value-initialized
    // to 0 = not declared) and no alert callback.
    TimeBudgets() noexcept = default;
    TimeBudgets(const TimeBudgets&) = delete;
    TimeBudgets& operator=(const TimeBudgets&) = delete;
    TimeBudgets(TimeBudgets&&) noexcept = delete;
    TimeBudgets& operator=(TimeBudgets&&) noexcept = delete;

    // Declares maxNs as the max duration of span id per frame. id must name a
    // catalog span (COUNT and beyond are invalid). Errors: INVALID_ARGUMENT
    // (id out of range), DUPLICATE_BUDGET (id already has a declared budget).
    // No-op on error: a failed declaration changes nothing. Registration is
    // init-time wiring, never a hot path.
    [[nodiscard]] ExpectedVoid setBudget(SpanId id, uint64_t maxNs) noexcept;

    // Declared budget of span id, or 0 when not declared. O(1), no
    // allocation. id must name a catalog span.
    [[nodiscard]] uint64_t budget(SpanId id) const noexcept;

    // Registers the alert callback; nullptr disables it. Called once per span
    // that exceeded its budget (see the header brief on the wiring point).
    void setAlert(BudgetExceededFn fn) noexcept;

    // Verifies profiler's frame capture against the declared budgets. Returns
    // the number of spans that exceeded their budget (0 = within budget).
    // Fires the alert callback once per exceeded span, in capture order, with
    // the exact id/durationNs/budgetNs. O(MAX_FRAME_SPANS), zero allocation.
    // NOT thread-safe: same frame thread as the Profiler.
    [[nodiscard]] uint32_t checkFrame(const Profiler& profiler) noexcept;

private:
    // Declared budgets by SpanId; 0 = not declared.
    std::array<uint64_t, SPAN_COUNT> m_budgetsNs{};
    BudgetExceededFn m_alert{nullptr};
};

} // namespace infinity::core

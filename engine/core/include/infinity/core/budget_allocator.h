// infinity/core/budget_allocator.h
//
// BudgetAllocator (F2.12, ADR-034, rules 03/08/11): a decorator over a backing
// Allocator that tracks the subsystem's NET LIVE BYTES against a fixed byte
// budget and alerts (exactly once per crossing) when the budget is exceeded.
// One per subsystem: a subsystem that grows without limit is detected early,
// not in release (ADR-034).
//
//   Decorator     - allocate()/deallocate()/supportsAlignment() delegate to
//                   the backing; the backing must outlive the decorator. The
//                   backing's result is returned unchanged: on failure nullptr
//                   propagates and never counts toward usedBytes.
//   Accounting    - m_used is the net live bytes: += size on every successful
//                   allocate, -= size (saturating at 0) on every non-null
//                   deallocate. A deallocate that would underflow means the
//                   caller violated the size-matching contract (Allocator
//                   contract): asserted in debug (ADR-003), saturating in
//                   release. deallocate(nullptr) is a no-op.
//   Budget        - budgetBytes == 0 means UNLIMITED: overBudget() is always
//                   false and the alert never fires. budgetBytes > 0 enables
//                   enforcement. A crossing (used transitions from <= budget
//                   to > budget) fires the alert EXACTLY ONCE; when used
//                   returns to <= budget the alert rearms and may fire again
//                   on the next crossing.
//   Alert         - Zero-allocation C-style callback (rule 03: callbacks are
//                   raw function pointers with a userData, never std::function
//                   nor shared_ptr). May be nullptr (no alert). The decorator
//                   deliberately does not bump diagnostics counters itself - a
//                   future runtime/logger consumer wires that (F2.12).
//   Determinism   - No hidden state (rule 11): all accounting lives in the
//                   object; construction allocates nothing (it stores scalars
//                   and pointers only), so the hot path stays allocation-free
//                   (rule 08). Accounting is single-threaded: an allocator is
//                   owned by one subsystem, never shared across threads.
//
// Lifetime: the backing Allocator must outlive the decorator. Copy and move
// are deleted like the concrete backends.
#pragma once

#include "infinity/core/allocator.h"

#include <cstddef>

namespace infinity::core {

// Per-subsystem budget decorator over a backing Allocator.
class BudgetAllocator final : public Allocator {
public:
    // Zero-allocation alert callback (rule 03). userData is opaque and passed
    // through unchanged; usedBytes/budgetBytes are the values at the crossing.
    using BudgetAlertFn = void (*)(void* userData, size_t usedBytes, size_t budgetBytes) noexcept;

    // Wraps backing with a budget of budgetBytes. budgetBytes == 0 means
    // UNLIMITED (never alerts, overBudget() always false). alertFn may be
    // nullptr (no alert); userData is passed to alertFn unchanged. backing
    // must outlive the decorator. Construction allocates nothing.
    BudgetAllocator(size_t budgetBytes, Allocator& backing, BudgetAlertFn alertFn = nullptr,
                    void* userData = nullptr) noexcept;

    BudgetAllocator(const BudgetAllocator&) = delete;
    BudgetAllocator& operator=(const BudgetAllocator&) = delete;
    BudgetAllocator(BudgetAllocator&&) noexcept = delete;
    BudgetAllocator& operator=(BudgetAllocator&&) noexcept = delete;

    ~BudgetAllocator() override = default;

    // Delegates to the backing and returns its result unchanged. On success
    // adds size to the live-byte count and fires the alert on a budget
    // crossing; on failure (backing returned nullptr) usedBytes is untouched.
    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override;

    // Delegates to the backing. Subtracts size from the live-byte count,
    // saturating at 0 (asserted in debug when it would underflow - a caller
    // size-matching violation). Passing nullptr is a no-op (Allocator
    // contract). Rearms the alert when used drops back to <= budget.
    void deallocate(void* ptr, size_t size) noexcept override;

    // Delegates to the backing.
    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override;

    // Live bytes currently outstanding through this decorator.
    [[nodiscard]] size_t usedBytes() const noexcept;

    // The declared budget; 0 means unlimited (never alerts).
    [[nodiscard]] size_t budgetBytes() const noexcept;

    // True iff budgetBytes != 0 && usedBytes > budgetBytes.
    [[nodiscard]] bool overBudget() const noexcept;

    // Replaces the alert callback and its userData. Passing nullptr disables
    // alerts. The armed state is unchanged.
    void setAlert(BudgetAlertFn alertFn, void* userData) noexcept;

private:
    Allocator& m_backing;    // must outlive the decorator
    size_t m_budgetBytes{0}; // 0 == unlimited
    size_t m_used{0};        // net live bytes outstanding
    bool m_alertArmed{true}; // fires once per <= -> > crossing
    BudgetAlertFn m_alertFn{nullptr};
    void* m_alertUserData{nullptr};
};

} // namespace infinity::core

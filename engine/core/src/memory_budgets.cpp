// src/memory_budgets.cpp
#include "infinity/core/memory_budgets.h"

#include <cassert>
#include <expected>
#include <utility>

namespace infinity::core {

namespace {

// True when id names a catalog subsystem (rule 04): an id at or beyond COUNT
// is a caller bug - a programming invariant, asserted in debug on the query
// paths and rejected as a recoverable error by registerBudget (rule 04).
[[nodiscard]] constexpr bool isValidBudgetId(BudgetId id) noexcept {
    return std::to_underlying(id) < BUDGET_COUNT;
}

} // namespace

ExpectedVoid MemoryBudgets::registerBudget(BudgetId id, BudgetAllocator& allocator) noexcept {
    if (!isValidBudgetId(id)) {
        return std::unexpected(CoreError::INVALID_ARGUMENT);
    }
    // NOLINTNEXTLINE(modernize-use-auto) -- rule 02: no auto for trivial types.
    const size_t index = static_cast<size_t>(id);
    if (m_allocators[index] != nullptr) {
        return std::unexpected(CoreError::DUPLICATE_BUDGET);
    }
    m_allocators[index] = &allocator;
    return {};
}

const BudgetAllocator* MemoryBudgets::budget(BudgetId id) const noexcept {
    assert(isValidBudgetId(id));
    return m_allocators[static_cast<size_t>(id)];
}

size_t MemoryBudgets::usedBytes(BudgetId id) const noexcept {
    assert(isValidBudgetId(id));
    const BudgetAllocator* allocator = m_allocators[static_cast<size_t>(id)];
    return allocator != nullptr ? allocator->usedBytes() : 0;
}

size_t MemoryBudgets::totalUsedBytes() const noexcept {
    size_t total = 0;
    for (const BudgetAllocator* allocator : m_allocators) {
        if (allocator != nullptr) {
            total += allocator->usedBytes();
        }
    }
    return total;
}

size_t MemoryBudgets::totalBudgetBytes() const noexcept {
    size_t total = 0;
    for (const BudgetAllocator* allocator : m_allocators) {
        if (allocator != nullptr) {
            total += allocator->budgetBytes();
        }
    }
    return total;
}

} // namespace infinity::core

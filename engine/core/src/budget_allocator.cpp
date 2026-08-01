// src/budget_allocator.cpp
#include "infinity/core/budget_allocator.h"

#include <cassert>

namespace infinity::core {

BudgetAllocator::BudgetAllocator(size_t budgetBytes, Allocator& backing, BudgetAlertFn alertFn,
                                 void* userData) noexcept
    : m_backing(backing), m_budgetBytes(budgetBytes), m_alertFn(alertFn),
      m_alertUserData(userData) {}

void* BudgetAllocator::allocate(size_t size, size_t alignment) noexcept {
    void* result = m_backing.allocate(size, alignment);
    if (result == nullptr) {
        return nullptr;
    }
    m_used += size;
    if (m_alertArmed && overBudget()) {
        m_alertArmed = false;
        if (m_alertFn != nullptr) {
            m_alertFn(m_alertUserData, m_used, m_budgetBytes);
        }
    }
    return result;
}

void BudgetAllocator::deallocate(void* ptr, size_t size) noexcept {
    if (ptr == nullptr) {
        return; // Allocator contract: no-op
    }
    m_backing.deallocate(ptr, size);
    assert(m_used >= size);                       // caller violated the size-matching contract
    m_used = (size < m_used) ? m_used - size : 0; // saturating at 0
    if (!overBudget()) {
        m_alertArmed = true;
    }
}

bool BudgetAllocator::supportsAlignment(size_t alignment) const noexcept {
    return m_backing.supportsAlignment(alignment);
}

size_t BudgetAllocator::usedBytes() const noexcept { return m_used; }

size_t BudgetAllocator::budgetBytes() const noexcept { return m_budgetBytes; }

bool BudgetAllocator::overBudget() const noexcept {
    return m_budgetBytes != 0 && m_used > m_budgetBytes;
}

void BudgetAllocator::setAlert(BudgetAlertFn alertFn, void* userData) noexcept {
    m_alertFn = alertFn;
    m_alertUserData = userData;
}

} // namespace infinity::core

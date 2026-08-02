// tests/core/memory_budgets_test.cpp
//
// MemoryBudgets contract tests (F2.12, ADR-034, rules 04/06/11): registerBudget
// + budget round-trip, every declared error branch (invalid id and duplicate
// registration) is a recoverable error (rule 04), unregistered ids query as
// null/zero, aggregates (totalUsedBytes/totalBudgetBytes) skip unregistered
// entries and treat an unlimited (budget 0) allocator as contributing 0, and a
// local instance keeps its registry fully isolated (rule 11: no global state).
#include "infinity/core/budget_allocator.h"
#include "infinity/core/memory_budgets.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <doctest/doctest.h>

namespace {

using infinity::core::Allocator;
using infinity::core::BudgetAllocator;
using infinity::core::BudgetId;
using infinity::core::CoreError;
using infinity::core::ExpectedVoid;
using infinity::core::MemoryBudgets;

// malloc-backed Allocator test double (rule 03: malloc stays inside allocator
// implementations): the budgets need a backing that can actually satisfy
// allocations so usedBytes can be exercised.
class MallocBacking final : public Allocator {
public:
    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override {
        if (!infinity::core::isValidAlignment(alignment) || alignment > alignof(std::max_align_t)) {
            return nullptr;
        }
        return std::malloc(size);
    }

    void deallocate(void* ptr, size_t size) noexcept override {
        (void)size;
        std::free(ptr);
    }

    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override {
        return infinity::core::isValidAlignment(alignment) &&
               alignment <= alignof(std::max_align_t);
    }
};

// Comparison isolated from CHECK so doctest never stringifies a CoreError
// operand (the error_test.cpp pattern).
bool isError(const ExpectedVoid& result, CoreError expected) noexcept {
    return !result.has_value() && result.error() == expected;
}

} // namespace

TEST_CASE("registerBudget and budget round-trip") {
    MallocBacking backing;
    BudgetAllocator allocator{256, backing};
    MemoryBudgets budgets;

    const ExpectedVoid result = budgets.registerBudget(BudgetId::ECS, allocator);
    CHECK(result.has_value());
    CHECK(budgets.budget(BudgetId::ECS) == &allocator);
    CHECK(budgets.usedBytes(BudgetId::ECS) == allocator.usedBytes());
    CHECK(budgets.usedBytes(BudgetId::ECS) == 0);
}

TEST_CASE("duplicate registration is a recoverable error") {
    MallocBacking backing;
    BudgetAllocator first{256, backing};
    BudgetAllocator second{128, backing};
    MemoryBudgets budgets;

    CHECK(budgets.registerBudget(BudgetId::CORE, first).has_value());
    CHECK(isError(budgets.registerBudget(BudgetId::CORE, second), CoreError::DUPLICATE_BUDGET));
    CHECK(budgets.budget(BudgetId::CORE) == &first);
}

TEST_CASE("invalid budget id is a recoverable error") {
    MallocBacking backing;
    BudgetAllocator allocator{256, backing};
    MemoryBudgets budgets;

    // NOLINTNEXTLINE(modernize-use-auto) -- rule 02: no auto for trivial types.
    const BudgetId atCount = static_cast<BudgetId>(BudgetId::COUNT);
    CHECK(isError(budgets.registerBudget(atCount, allocator), CoreError::INVALID_ARGUMENT));

    // A far-out id proves the range check, not just the == COUNT check. No
    // auto for trivial types (rule 02); the cast is a deliberate out-of-range.
    // NOLINTNEXTLINE(modernize-use-auto, clang-analyzer-optin.core.EnumCastOutOfRange)
    const BudgetId farOut = static_cast<BudgetId>(9999);
    CHECK(isError(budgets.registerBudget(farOut, allocator), CoreError::INVALID_ARGUMENT));
}

TEST_CASE("unregistered budget id queries as null and zero") {
    MallocBacking backing;
    BudgetAllocator allocator{256, backing};
    MemoryBudgets budgets;

    CHECK(budgets.registerBudget(BudgetId::AI, allocator).has_value());
    CHECK(budgets.budget(BudgetId::NETWORK) == nullptr);
    CHECK(budgets.usedBytes(BudgetId::NETWORK) == 0);
}

TEST_CASE("totalUsedBytes and totalBudgetBytes aggregate registered entries only") {
    MallocBacking backing;
    BudgetAllocator coreBudget{100, backing};
    BudgetAllocator runtimeBudget{200, backing};
    BudgetAllocator rendererBudget{0, backing}; // unlimited: 0 toward totalBudgetBytes
    BudgetAllocator aiBudget{150, backing};
    MemoryBudgets budgets;

    CHECK(budgets.registerBudget(BudgetId::CORE, coreBudget).has_value());
    CHECK(budgets.registerBudget(BudgetId::RUNTIME, runtimeBudget).has_value());
    CHECK(budgets.registerBudget(BudgetId::RENDERER, rendererBudget).has_value());
    CHECK(budgets.registerBudget(BudgetId::AI, aiBudget).has_value());

    void* coreBlock = coreBudget.allocate(40, 1);
    void* runtimeBlock = runtimeBudget.allocate(60, 1);
    void* rendererBlock = rendererBudget.allocate(30, 1);
    CHECK(coreBlock != nullptr);
    CHECK(runtimeBlock != nullptr);
    CHECK(rendererBlock != nullptr);

    CHECK(budgets.totalUsedBytes() == 130);
    CHECK(budgets.totalBudgetBytes() == 450);

    coreBudget.deallocate(coreBlock, 40);
    runtimeBudget.deallocate(runtimeBlock, 60);
    rendererBudget.deallocate(rendererBlock, 30);
}

TEST_CASE("local instance keeps its registry fully isolated") {
    MallocBacking backing;
    BudgetAllocator allocator{256, backing};
    MemoryBudgets local;

    CHECK(local.registerBudget(BudgetId::ECS, allocator).has_value());
    CHECK(local.budget(BudgetId::ECS) == &allocator);
    CHECK(local.totalUsedBytes() == 0);

    // A second instance never sees the first one's registry (rule 11: state
    // lives in explicit objects, never in a global singleton).
    MemoryBudgets other;
    CHECK(other.budget(BudgetId::ECS) == nullptr);
    CHECK(other.totalUsedBytes() == 0);
    CHECK(other.totalBudgetBytes() == 0);
}

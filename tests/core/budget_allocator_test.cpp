// tests/core/budget_allocator_test.cpp
//
// BudgetAllocator contract tests (F2.12, ADR-034, rule 06): exact net live
// byte accounting across alloc/dealloc sequences, the exactly-once alert on a
// budget crossing (and re-arm when back at or under budget), the unlimited
// budget-0 contract, no-op deallocate(nullptr), backing failure propagation
// (nullptr that never counts toward usedBytes), delegation of
// supportsAlignment and of the typed allocateObject/deallocateObject paths, and
// determinism (the same sequence twice yields the same accounting, rule 11).
#include "infinity/core/budget_allocator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

#include <doctest/doctest.h>

namespace {

using infinity::core::Allocator;
using infinity::core::BudgetAllocator;

// Struct with a non-trivial size used to exercise the typed allocation path.
struct Point {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

// malloc-backed Allocator test double (rule 03: malloc stays inside allocator
// implementations): forwards allocate/deallocate to the C runtime so the
// decorator has a real backing to delegate to.
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

// Allocator test double that always fails: exercises the "failure propagates
// and never counts toward usedBytes" contract.
class FailingBacking final : public Allocator {
public:
    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override {
        (void)size;
        (void)alignment;
        return nullptr;
    }

    void deallocate(void* ptr, size_t size) noexcept override {
        (void)ptr;
        (void)size;
    }

    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override {
        return infinity::core::isValidAlignment(alignment);
    }
};

// Alert instrumentation. The callback is a raw function pointer (rule 03), so
// call history lives in file-scope harness state, not engine state (rule 11) -
// the system_registry_test pattern. Each alert test resets it before running.
struct AlertRecorder {
    int fireCount{0};
    size_t lastUsed{0};
    size_t lastBudget{0};
    void* lastUserData{nullptr};
};

AlertRecorder g_recorder;

void recordAlert(void* userData, size_t usedBytes, size_t budgetBytes) noexcept {
    g_recorder.lastUserData = userData;
    g_recorder.lastUsed = usedBytes;
    g_recorder.lastBudget = budgetBytes;
    ++g_recorder.fireCount;
}

// The decorator is non-relocatable (copy and move deleted), like the concrete
// backends (compile-time half of the wrapping contract).
static_assert(!std::is_copy_constructible_v<BudgetAllocator>);
static_assert(!std::is_copy_assignable_v<BudgetAllocator>);
static_assert(!std::is_move_constructible_v<BudgetAllocator>);
static_assert(!std::is_move_assignable_v<BudgetAllocator>);

// Runs a fixed alloc/dealloc sequence and records usedBytes after each step.
// Returns false when any allocation failed (the steps are then meaningless).
bool runSequence(BudgetAllocator& budget, std::array<size_t, 6>& steps) {
    void* a = budget.allocate(16, 1);
    void* b = budget.allocate(8, 1);
    steps[0] = budget.usedBytes();
    budget.deallocate(b, 8);
    steps[1] = budget.usedBytes();
    void* c = budget.allocate(32, 1);
    steps[2] = budget.usedBytes();
    budget.deallocate(a, 16);
    steps[3] = budget.usedBytes();
    void* d = budget.allocate(4, 1);
    steps[4] = budget.usedBytes();
    budget.deallocate(c, 32);
    budget.deallocate(d, 4);
    steps[5] = budget.usedBytes();
    return a != nullptr && b != nullptr && c != nullptr && d != nullptr;
}

} // namespace

TEST_CASE("BudgetAllocator reports exact live bytes after alloc/dealloc sequences") {
    MallocBacking backing;
    BudgetAllocator budget{1024, backing};

    void* a = budget.allocate(100, 1);
    CHECK(a != nullptr);
    CHECK(budget.usedBytes() == 100);

    void* b = budget.allocate(50, 1);
    CHECK(b != nullptr);
    CHECK(budget.usedBytes() == 150);

    budget.deallocate(b, 50);
    CHECK(budget.usedBytes() == 100);

    budget.deallocate(a, 100);
    CHECK(budget.usedBytes() == 0);
}

TEST_CASE("alert fires exactly once on the budget crossing and rearms below budget") {
    MallocBacking backing;
    BudgetAllocator budget{100, backing, recordAlert, &g_recorder};
    g_recorder = AlertRecorder{};

    void* a = budget.allocate(100, 1); // used == budget: not over yet
    CHECK(a != nullptr);
    CHECK_FALSE(budget.overBudget());
    CHECK(g_recorder.fireCount == 0);

    void* b = budget.allocate(1, 1); // 101 > 100: crossing fires exactly once
    CHECK(b != nullptr);
    CHECK(budget.overBudget());
    CHECK(g_recorder.fireCount == 1);
    CHECK(g_recorder.lastUsed == 101);
    CHECK(g_recorder.lastBudget == 100);
    CHECK(g_recorder.lastUserData == &g_recorder);

    void* c = budget.allocate(50, 1); // 151 still over: no second fire
    CHECK(c != nullptr);
    CHECK(g_recorder.fireCount == 1);

    budget.deallocate(c, 50); // 101: still over budget, no rearm yet
    CHECK(budget.overBudget());

    budget.deallocate(b, 1); // 100 <= budget: rearm
    CHECK_FALSE(budget.overBudget());

    void* d = budget.allocate(100, 1); // 200 > 100: fires again
    CHECK(d != nullptr);
    CHECK(g_recorder.fireCount == 2);
    CHECK(g_recorder.lastUsed == 200);

    budget.deallocate(d, 100);
    budget.deallocate(a, 100);
}

TEST_CASE("budget zero is unlimited and never alerts") {
    MallocBacking backing;
    BudgetAllocator budget{0, backing, recordAlert, &g_recorder};
    g_recorder = AlertRecorder{};

    std::array<void*, 4> blocks{};
    for (void*& block : blocks) {
        block = budget.allocate(256, 1);
        CHECK(block != nullptr);
    }

    CHECK(budget.budgetBytes() == 0);
    CHECK(budget.usedBytes() == 4 * 256);
    CHECK_FALSE(budget.overBudget());
    CHECK(g_recorder.fireCount == 0);

    for (void*& block : blocks) {
        budget.deallocate(block, 256);
    }
    CHECK(budget.usedBytes() == 0);
}

TEST_CASE("deallocate of nullptr is a no-op and usedBytes never goes negative") {
    MallocBacking backing;
    BudgetAllocator budget{1024, backing};

    budget.deallocate(nullptr, 64);
    CHECK(budget.usedBytes() == 0);
    CHECK_FALSE(budget.overBudget());

    void* a = budget.allocate(128, 1);
    CHECK(a != nullptr);
    budget.deallocate(a, 128);
    CHECK(budget.usedBytes() == 0);
    CHECK_FALSE(budget.overBudget());
}

TEST_CASE("backing failure propagates nullptr and does not count toward usedBytes") {
    FailingBacking backing;
    BudgetAllocator budget{64, backing};

    CHECK(budget.allocate(16, 1) == nullptr);
    CHECK(budget.allocate(64, 1) == nullptr);
    CHECK(budget.usedBytes() == 0);
    CHECK_FALSE(budget.overBudget());
}

TEST_CASE("supportsAlignment delegates to the backing") {
    MallocBacking backing;
    BudgetAllocator budget{1024, backing};

    CHECK(budget.supportsAlignment(1));
    CHECK(budget.supportsAlignment(2));
    CHECK(budget.supportsAlignment(8));
    CHECK(budget.supportsAlignment(alignof(std::max_align_t)));
    CHECK_FALSE(budget.supportsAlignment(0));
    CHECK_FALSE(budget.supportsAlignment(3));
    CHECK_FALSE(budget.supportsAlignment(alignof(std::max_align_t) * 2));
}

TEST_CASE("allocateObject and deallocateObject delegate through the decorator") {
    MallocBacking backing;
    BudgetAllocator budget{1024, backing};

    auto* first = budget.allocateObject<Point>(alignof(std::max_align_t));
    CHECK(first != nullptr);
    CHECK(budget.usedBytes() == sizeof(Point));
    CHECK(reinterpret_cast<uintptr_t>(first) % alignof(std::max_align_t) == 0);

    auto* second = budget.allocateObject<Point>(alignof(std::max_align_t));
    CHECK(second != nullptr);
    CHECK(budget.usedBytes() == 2 * sizeof(Point));

    budget.deallocateObject(first);
    CHECK(budget.usedBytes() == sizeof(Point));
    budget.deallocateObject(second);
    CHECK(budget.usedBytes() == 0);
}

TEST_CASE("same alloc/dealloc sequence twice yields identical usedBytes") {
    MallocBacking backing;
    BudgetAllocator budget{1024, backing};

    std::array<size_t, 6> firstSteps{};
    std::array<size_t, 6> secondSteps{};
    CHECK(runSequence(budget, firstSteps));
    CHECK(runSequence(budget, secondSteps));

    for (size_t i = 0; i < firstSteps.size(); ++i) {
        CAPTURE(i);
        CHECK(firstSteps[i] == secondSteps[i]);
    }
    CHECK(budget.usedBytes() == 0);
}

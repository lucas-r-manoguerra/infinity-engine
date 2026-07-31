// tests/core/pool_allocator_test.cpp
//
// PoolAllocator contract tests (F2.2, rule 06): distinct slot addresses,
// alignment, exhaustion, free-list reuse, usedCount/capacity accounting,
// deterministic order, the backing traffic contract (one acquisition, one
// release), empty pool on backing failure, supportsAlignment and the typed
// allocation path.
#include "infinity/core/pool_allocator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

#include <doctest/doctest.h>

namespace {

// Pool alignment used throughout these tests. The pool hands the backing a
// block aligned to this value, and the backing contract (malloc-backed,
// rule 03) rejects alignment above alignof(std::max_align_t) -- which is 8 on
// MSVC and 16 on GCC/Clang. Tests must not hardcode 16: an empty pool fails
// every allocation CHECK.
constexpr size_t kAlignment = alignof(std::max_align_t);

// Struct large enough to exercise the typed allocation path (elementSize
// must be >= alignment; max_align_t <= 16 everywhere, so 16 bytes always fits).
struct Vector4 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{0.0f};
};

static_assert(sizeof(Vector4) >= 16);

// Counting Allocator test double: forwards to the C runtime and tallies
// allocate/deallocate calls so tests can verify the pool's backing traffic.
class CountingAllocator final : public infinity::core::Allocator {
public:
    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override {
        if (!infinity::core::isValidAlignment(alignment) || alignment > alignof(std::max_align_t)) {
            return nullptr;
        }
        ++m_allocationCount;
        return std::malloc(size);
    }

    void deallocate(void* ptr, size_t size) noexcept override {
        (void)size;
        ++m_deallocationCount;
        std::free(ptr);
    }

    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override {
        return infinity::core::isValidAlignment(alignment);
    }

    [[nodiscard]] size_t allocationCount() const noexcept { return m_allocationCount; }

    [[nodiscard]] size_t deallocationCount() const noexcept { return m_deallocationCount; }

private:
    size_t m_allocationCount{0};
    size_t m_deallocationCount{0};
};

// Allocator test double that always fails: exercises the empty-pool contract
// (capacity 0, every allocate returns nullptr) when the block cannot be
// acquired.
class FailingAllocator final : public infinity::core::Allocator {
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

// The pool is non-relocatable: copy and move are deleted (compile-time half of
// the block-owning contract).
static_assert(!std::is_copy_constructible_v<infinity::core::PoolAllocator>);
static_assert(!std::is_copy_assignable_v<infinity::core::PoolAllocator>);
static_assert(!std::is_move_constructible_v<infinity::core::PoolAllocator>);
static_assert(!std::is_move_assignable_v<infinity::core::PoolAllocator>);

} // namespace

TEST_CASE("allocate hands out distinct slot addresses") {
    CountingAllocator backing;
    infinity::core::PoolAllocator pool{16, kAlignment, 3, backing};

    auto* first = pool.allocate(16, kAlignment);
    auto* second = pool.allocate(16, kAlignment);
    auto* third = pool.allocate(16, kAlignment);

    CHECK(first != nullptr);
    CHECK(second != nullptr);
    CHECK(third != nullptr);
    CHECK(first != second);
    CHECK(first != third);
    CHECK(second != third);
}

TEST_CASE("every slot is aligned to the pool alignment") {
    CountingAllocator backing;
    infinity::core::PoolAllocator pool{16, kAlignment, 4, backing};

    for (size_t i = 0; i < pool.capacity(); ++i) {
        void* slot = pool.allocate(16, kAlignment);
        CHECK(slot != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(slot) % kAlignment == 0);
    }
}

TEST_CASE("allocation beyond capacity returns nullptr and usedCount saturates") {
    CountingAllocator backing;
    infinity::core::PoolAllocator pool{16, kAlignment, 3, backing};

    auto* first = pool.allocate(16, kAlignment);
    auto* second = pool.allocate(16, kAlignment);
    auto* third = pool.allocate(16, kAlignment);
    CHECK(first != nullptr);
    CHECK(second != nullptr);
    CHECK(third != nullptr);
    CHECK(pool.usedCount() == 3);

    CHECK(pool.allocate(16, kAlignment) == nullptr);
    CHECK(pool.usedCount() == 3);
}

TEST_CASE("deallocate returns the slot to the free list for immediate reuse") {
    CountingAllocator backing;
    infinity::core::PoolAllocator pool{16, kAlignment, 3, backing};

    auto* first = pool.allocate(16, kAlignment);
    auto* second = pool.allocate(16, kAlignment);
    CHECK(first != nullptr);
    CHECK(second != nullptr);
    CHECK(pool.usedCount() == 2);

    pool.deallocate(second, 16);
    CHECK(pool.usedCount() == 1);

    auto* again = pool.allocate(16, kAlignment);
    CHECK(again == second);
    CHECK(pool.usedCount() == 2);
}

TEST_CASE("fully freed pool serves a full cycle of allocations again") {
    CountingAllocator backing;
    infinity::core::PoolAllocator pool{16, kAlignment, 3, backing};

    std::array<void*, 3> firstCycle{};
    for (void*& slot : firstCycle) {
        slot = pool.allocate(16, kAlignment);
        CHECK(slot != nullptr);
    }

    for (void* slot : firstCycle) {
        pool.deallocate(slot, 16);
    }
    CHECK(pool.usedCount() == 0);

    std::array<void*, 3> secondCycle{};
    for (void*& slot : secondCycle) {
        slot = pool.allocate(16, kAlignment);
        CHECK(slot != nullptr);
    }
    CHECK(pool.usedCount() == 3);
}

TEST_CASE("usedCount and capacity stay correct through mixed sequences") {
    CountingAllocator backing;
    infinity::core::PoolAllocator pool{16, kAlignment, 4, backing};

    auto* a = pool.allocate(16, kAlignment);
    auto* b = pool.allocate(16, kAlignment);
    CHECK(pool.capacity() == 4);
    CHECK(pool.usedCount() == 2);

    pool.deallocate(a, 16);
    CHECK(pool.usedCount() == 1);

    auto* c = pool.allocate(16, kAlignment);
    auto* d = pool.allocate(16, kAlignment);
    CHECK(c != nullptr);
    CHECK(d != nullptr);
    CHECK(pool.usedCount() == 3);

    pool.deallocate(c, 16);
    pool.deallocate(b, 16);
    pool.deallocate(d, 16);
    CHECK(pool.usedCount() == 0);
    CHECK(pool.capacity() == 4);
}

TEST_CASE("two pools built with the same parameters hand out slots in the same order") {
    CountingAllocator backingA;
    CountingAllocator backingB;
    infinity::core::PoolAllocator poolA{16, kAlignment, 4, backingA};
    infinity::core::PoolAllocator poolB{16, kAlignment, 4, backingB};

    std::array<void*, 4> slotsA{};
    std::array<void*, 4> slotsB{};
    for (size_t i = 0; i < 4; ++i) {
        slotsA[i] = poolA.allocate(16, kAlignment);
        slotsB[i] = poolB.allocate(16, kAlignment);
    }

    for (size_t i = 0; i < 4; ++i) {
        CHECK(slotsA[i] != nullptr);
        CHECK(slotsB[i] != nullptr);
        const auto offsetA =
            reinterpret_cast<uintptr_t>(slotsA[i]) - reinterpret_cast<uintptr_t>(slotsA[0]);
        const auto offsetB =
            reinterpret_cast<uintptr_t>(slotsB[i]) - reinterpret_cast<uintptr_t>(slotsB[0]);
        CHECK(offsetA == offsetB);
    }
}

TEST_CASE("backing is hit once at construction and once at destruction") {
    CountingAllocator backing;
    {
        infinity::core::PoolAllocator pool{16, kAlignment, 8, backing};
        CHECK(backing.allocationCount() == 1);
        CHECK(backing.deallocationCount() == 0);
        (void)pool.allocate(16, kAlignment);
    }
    CHECK(backing.deallocationCount() == 1);
}

TEST_CASE("empty pool when the backing cannot provide the block") {
    FailingAllocator backing;
    infinity::core::PoolAllocator pool{16, kAlignment, 4, backing};

    CHECK(pool.capacity() == 0);
    CHECK(pool.usedCount() == 0);
    CHECK(pool.allocate(16, kAlignment) == nullptr);
    CHECK_FALSE(pool.supportsAlignment(kAlignment));
    CHECK_FALSE(pool.supportsAlignment(1));
}

TEST_CASE("supportsAlignment accepts only the pool alignment and byte alignment") {
    CountingAllocator backing;
    infinity::core::PoolAllocator pool{16, kAlignment, 4, backing};

    CHECK(pool.supportsAlignment(kAlignment));
    CHECK(pool.supportsAlignment(1));
    CHECK_FALSE(pool.supportsAlignment(0));
    CHECK_FALSE(pool.supportsAlignment(2));
    CHECK_FALSE(pool.supportsAlignment(4));
    CHECK_FALSE(pool.supportsAlignment(kAlignment / 2));
    CHECK_FALSE(pool.supportsAlignment(kAlignment * 2));
}

TEST_CASE("allocateObject with the pool alignment returns an aligned object") {
    CountingAllocator backing;
    infinity::core::PoolAllocator pool{sizeof(Vector4), kAlignment, 4, backing};

    auto* vector = pool.allocateObject<Vector4>(kAlignment);

    CHECK(vector != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(vector) % kAlignment == 0);
    CHECK(pool.usedCount() == 1);

    pool.deallocateObject(vector);
    CHECK(pool.usedCount() == 0);
}

TEST_CASE("standalone pool is backed by an internal malloc backend") {
    infinity::core::PoolAllocator pool{16, kAlignment, 4};

    CHECK(pool.capacity() == 4);

    auto* first = pool.allocate(16, kAlignment);
    CHECK(first != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(first) % kAlignment == 0);

    pool.deallocate(first, 16);

    auto* again = pool.allocate(16, kAlignment);
    CHECK(again == first);
}

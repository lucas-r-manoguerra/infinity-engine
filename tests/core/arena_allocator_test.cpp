// tests/core/arena_allocator_test.cpp
//
// ArenaAllocator contract tests (F2.2, rule 06): bump alignment, monotonic
// growth, deterministic reset, exhaustion, no-op deallocate, typed allocation
// and the backing traffic contract (one acquisition, one release).
#include "infinity/core/arena_allocator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

#include <doctest/doctest.h>

namespace {

// Struct with a non-trivial size used to exercise the typed allocation path.
struct Point {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

// Counting Allocator test double: forwards to the C runtime and tallies
// allocate/deallocate calls so tests can verify the arena's backing traffic.
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

// Allocator test double that always fails: exercises the empty-arena contract
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

// The arena is non-relocatable: copy and move are deleted (compile-time half
// of the frame-scoped contract).
static_assert(!std::is_copy_constructible_v<infinity::core::ArenaAllocator>);
static_assert(!std::is_copy_assignable_v<infinity::core::ArenaAllocator>);
static_assert(!std::is_move_constructible_v<infinity::core::ArenaAllocator>);
static_assert(!std::is_move_assignable_v<infinity::core::ArenaAllocator>);

} // namespace

TEST_CASE("aligned allocations land on correctly aligned addresses") {
    CountingAllocator backing;
    infinity::core::ArenaAllocator arena{1024, backing};

    const std::array<size_t, 5> alignments{1, 2, 4, 8, 16};
    for (const size_t alignment : alignments) {
        void* block = arena.allocate(1, alignment);
        CHECK(block != nullptr);
        CHECK(reinterpret_cast<uintptr_t>(block) % alignment == 0);
    }
}

TEST_CASE("second allocation bumps past the first") {
    CountingAllocator backing;
    infinity::core::ArenaAllocator arena{1024, backing};

    auto* first = arena.allocate(8, 8);
    auto* second = arena.allocate(8, 8);

    CHECK(first != nullptr);
    CHECK(second != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(second) > reinterpret_cast<uintptr_t>(first));
}

TEST_CASE("reset then the same request sequence returns the same addresses") {
    CountingAllocator backing;
    infinity::core::ArenaAllocator arena{1024, backing};

    auto* firstPassA = arena.allocate(16, 8);
    auto* firstPassB = arena.allocate(4, 1);

    arena.reset();

    auto* secondPassA = arena.allocate(16, 8);
    auto* secondPassB = arena.allocate(4, 1);

    CHECK(firstPassA == secondPassA);
    CHECK(firstPassB == secondPassB);
    CHECK(arena.usedBytes() == 20);
}

TEST_CASE("request exceeding the remaining capacity returns nullptr") {
    CountingAllocator backing;
    infinity::core::ArenaAllocator arena{32, backing};

    auto* head = arena.allocate(16, 1);
    CHECK(head != nullptr);
    CHECK(arena.usedBytes() == 16);

    CHECK(arena.allocate(32, 1) == nullptr);
    CHECK(arena.usedBytes() == 16);

    auto* tail = arena.allocate(16, 1);
    CHECK(tail != nullptr);
    CHECK(arena.usedBytes() == 32);

    CHECK(arena.allocate(1, 1) == nullptr);
}

TEST_CASE("usedBytes and capacityBytes account for alignment padding") {
    CountingAllocator backing;
    infinity::core::ArenaAllocator arena{64, backing};

    auto* a = arena.allocate(3, 1);
    auto* b = arena.allocate(4, 8);
    auto* c = arena.allocate(1, 16);

    CHECK(a != nullptr);
    CHECK(b != nullptr);
    CHECK(c != nullptr);
    CHECK(arena.usedBytes() == 17);
    CHECK(arena.capacityBytes() == 64);
}

TEST_CASE("deallocate is a no-op and only reset makes the block reusable") {
    CountingAllocator backing;
    infinity::core::ArenaAllocator arena{1024, backing};

    auto* first = arena.allocate(16, 8);
    CHECK(first != nullptr);

    arena.deallocate(first, 16);
    auto* bytes = static_cast<unsigned char*>(first);
    bytes[0] = 0xAB;
    CHECK(bytes[0] == 0xAB);

    auto* second = arena.allocate(16, 8);
    CHECK(second != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(second) > reinterpret_cast<uintptr_t>(first));

    arena.deallocate(nullptr, 16);

    arena.reset();
    auto* again = arena.allocate(16, 8);
    CHECK(again == first);
}

TEST_CASE("allocateObject with 16-byte alignment returns an aligned object") {
    CountingAllocator backing;
    infinity::core::ArenaAllocator arena{1024, backing};

    auto* point = arena.allocateObject<Point>(16);

    CHECK(point != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(point) % 16 == 0);
    CHECK(arena.usedBytes() == sizeof(Point));
}

TEST_CASE("backing is hit once at construction and once at destruction") {
    CountingAllocator backing;
    {
        infinity::core::ArenaAllocator arena{1024, backing};
        CHECK(backing.allocationCount() == 1);
        CHECK(backing.deallocationCount() == 0);
    }
    CHECK(backing.deallocationCount() == 1);
}

TEST_CASE("empty arena when the backing cannot provide the block") {
    FailingAllocator backing;
    infinity::core::ArenaAllocator arena{1024, backing};

    CHECK(arena.capacityBytes() == 0);
    CHECK(arena.usedBytes() == 0);
    CHECK(arena.allocate(8, 1) == nullptr);
    CHECK(arena.allocate(1, 16) == nullptr);
    CHECK(arena.allocateObject<Point>(16) == nullptr);

    arena.reset();
    CHECK(arena.capacityBytes() == 0);
}

TEST_CASE("supportsAlignment covers the block alignment range") {
    CountingAllocator backing;
    infinity::core::ArenaAllocator arena{1024, backing};

    CHECK(arena.supportsAlignment(1));
    CHECK(arena.supportsAlignment(2));
    CHECK(arena.supportsAlignment(4));
    CHECK(arena.supportsAlignment(8));
    CHECK(arena.supportsAlignment(16));
    CHECK(arena.supportsAlignment(alignof(std::max_align_t)));
    CHECK_FALSE(arena.supportsAlignment(0));
    CHECK_FALSE(arena.supportsAlignment(3));
    CHECK_FALSE(arena.supportsAlignment(alignof(std::max_align_t) * 2));
    CHECK(arena.maxAlignment() == alignof(std::max_align_t));
}

TEST_CASE("standalone arena is backed by an internal malloc backend") {
    infinity::core::ArenaAllocator arena{256};

    CHECK(arena.capacityBytes() == 256);

    auto* first = arena.allocate(32, 8);
    CHECK(first != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(first) % 8 == 0);

    arena.reset();

    auto* second = arena.allocate(32, 8);
    CHECK(second == first);
}

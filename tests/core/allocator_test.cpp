// tests/core/allocator_test.cpp
//
// Contract tests for the Allocator interface (F2.1, rule 06). Backends are
// F2.2, so a minimal test double exercises the contract the whole engine
// allocates through: abstract base, alignment validity, typed round-trip and
// the null-on-failure path.
#include "infinity/core/allocator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <doctest/doctest.h>

namespace {

// Struct with a non-trivial size used to exercise typed allocation.
struct Point {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

// Minimal Allocator test double: bumps a fixed block under a byte budget and
// returns nullptr when a request exceeds it or uses an invalid alignment,
// which is exactly the failure contract of the interface.
class BumpAllocator final : public infinity::core::Allocator {
public:
    explicit BumpAllocator(size_t budget) noexcept : m_budget(budget) {}

    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override {
        if (!infinity::core::isValidAlignment(alignment) || m_used + size > m_budget) {
            return nullptr;
        }
        void* block = m_storage.data() + m_used;
        m_used += size;
        ++m_allocationCount;
        return block;
    }

    void deallocate(void* ptr, size_t size) noexcept override {
        if (ptr == nullptr) {
            return;
        }
        m_freedBytes += size;
    }

    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override {
        return infinity::core::isValidAlignment(alignment);
    }

    [[nodiscard]] size_t usedBytes() const noexcept { return m_used; }

    [[nodiscard]] size_t allocationCount() const noexcept { return m_allocationCount; }

    [[nodiscard]] size_t freedBytes() const noexcept { return m_freedBytes; }

private:
    alignas(16) std::array<unsigned char, 1024> m_storage{};
    size_t m_used{0};
    size_t m_allocationCount{0};
    size_t m_freedBytes{0};
    size_t m_budget{0};
};

// The interface is abstract: no Allocator value can exist until a backend
// implements it (F2.2). This is the compile-time half of the contract.
static_assert(std::is_abstract_v<infinity::core::Allocator>);

} // namespace

TEST_CASE("isValidAlignment accepts powers of two and rejects anything else") {
    CHECK(infinity::core::isValidAlignment(1));
    CHECK(infinity::core::isValidAlignment(2));
    CHECK(infinity::core::isValidAlignment(4));
    CHECK(infinity::core::isValidAlignment(8));
    CHECK(infinity::core::isValidAlignment(16));
    CHECK_FALSE(infinity::core::isValidAlignment(0));
    CHECK_FALSE(infinity::core::isValidAlignment(3));
    CHECK_FALSE(infinity::core::isValidAlignment(12));
    CHECK_FALSE(infinity::core::isValidAlignment(17));
}

TEST_CASE("supportsAlignment accepts byte-aligned up to 16-byte alignment") {
    BumpAllocator allocator{1024};
    CHECK(allocator.supportsAlignment(1));
    CHECK(allocator.supportsAlignment(2));
    CHECK(allocator.supportsAlignment(4));
    CHECK(allocator.supportsAlignment(8));
    CHECK(allocator.supportsAlignment(16));
    CHECK_FALSE(allocator.supportsAlignment(0));
    CHECK_FALSE(allocator.supportsAlignment(6));
}

TEST_CASE("allocateObject returns a block of the right size and alignment") {
    BumpAllocator allocator{1024};
    const size_t countBefore = allocator.allocationCount();
    const size_t bytesBefore = allocator.usedBytes();

    auto* point = allocator.allocateObject<Point>(16);

    CHECK(point != nullptr);
    CHECK(allocator.allocationCount() == countBefore + 1);
    CHECK(allocator.usedBytes() == bytesBefore + sizeof(Point));
    CHECK(reinterpret_cast<uintptr_t>(point) % 16 == 0);
}

TEST_CASE("deallocateObject accepts a valid pointer without crashing") {
    BumpAllocator allocator{1024};
    auto* point = allocator.allocateObject<Point>(16);
    CHECK(point != nullptr);

    allocator.deallocateObject(point);

    CHECK(allocator.freedBytes() == sizeof(Point));
}

TEST_CASE("allocateObject returns nullptr when the budget is exceeded") {
    BumpAllocator allocator{4};
    auto* point = allocator.allocateObject<Point>(1);
    CHECK(point == nullptr);
}

TEST_CASE("allocate returns nullptr for an invalid alignment request") {
    BumpAllocator allocator{1024};
    void* block = allocator.allocate(16, 3);
    CHECK(block == nullptr);
}

TEST_CASE("deallocate with nullptr is a no-op") {
    BumpAllocator allocator{1024};
    allocator.deallocate(nullptr, 0);
    CHECK(allocator.freedBytes() == 0);
}

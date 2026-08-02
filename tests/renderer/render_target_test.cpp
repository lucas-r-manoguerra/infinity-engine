// tests/renderer/render_target_test.cpp
//
// Contract tests for RenderTarget (F4.1, ADR-005/041): the factory validates
// size and allocation failures, the buffer is zero-filled on creation so a
// target is deterministic from birth (rule 11), pixels() exposes the BGRA32
// backing, checksum() is a stable FNV-1a-64 over the raw bytes (rule 11), and
// move semantics transfer ownership so the exact allocation size is returned
// to the allocator exactly once (ADR-005). The allocator double mirrors
// tests/platform/window_test.cpp.
#include "infinity/renderer/render_target.h"

#include "infinity/core/allocator.h"
#include "infinity/core/error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>

#include <doctest/doctest.h>

namespace {

// Minimal Allocator double: bumps a fixed block under a byte budget and counts
// frees so tests can assert the exact release contract (window_test.cpp).
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
    alignas(16) std::array<unsigned char, 65536> m_storage{};
    size_t m_used{0};
    size_t m_allocationCount{0};
    size_t m_freedBytes{0};
    size_t m_budget{0};
};

// Errors are asserted by category, never by enum equality, so doctest never
// stringifies a RenderError operand (ADL note).
[[nodiscard]] bool isMappedTo(infinity::renderer::RenderError code,
                              infinity::core::ErrorCategory category) noexcept {
    return infinity::renderer::categoryOf(code) == category;
}

// Independent FNV-1a-64 reference over the raw bytes of the pixel buffer: the
// checksum contract is "deterministic and byte-stable", so the test reimplements
// the hash instead of trusting the library's version (rule 11).
[[nodiscard]] std::uint64_t fnv1aBytes(const std::uint32_t* pixels, size_t pixelCount) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(pixels);
    for (size_t i = 0; i < pixelCount * sizeof(std::uint32_t); ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

TEST_CASE("createRenderTarget returns a zero-filled target honoring the size") {
    BumpAllocator allocator{65536};

    const auto result = infinity::renderer::createRenderTarget(4, 4, allocator);

    CHECK(result.has_value());
    if (!result.has_value()) {
        return;
    }
    CHECK((*result).width() == 4);
    CHECK((*result).height() == 4);
    const std::span<const std::uint32_t> pixels = (*result).pixels();
    CHECK(pixels.size() == 16);
    for (const std::uint32_t pixel : pixels) {
        CHECK(pixel == 0);
    }
}

TEST_CASE("createRenderTarget rejects a zero dimension") {
    BumpAllocator allocator{65536};

    const auto zeroWidth = infinity::renderer::createRenderTarget(0, 4, allocator);
    const auto zeroHeight = infinity::renderer::createRenderTarget(4, 0, allocator);

    CHECK_FALSE(zeroWidth.has_value());
    CHECK(isMappedTo(zeroWidth.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
    CHECK_FALSE(zeroHeight.has_value());
    CHECK(isMappedTo(zeroHeight.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
}

TEST_CASE("createRenderTarget reports an allocation failure cleanly") {
    BumpAllocator allocator{4};

    const auto result = infinity::renderer::createRenderTarget(4, 4, allocator);

    CHECK_FALSE(result.has_value());
    CHECK(isMappedTo(result.error(), infinity::core::ErrorCategory::RESOURCE));
    CHECK(allocator.allocationCount() == 0);
}

TEST_CASE("checksum is deterministic and matches an independent FNV-1a") {
    BumpAllocator allocator{65536};
    auto result = infinity::renderer::createRenderTarget(4, 4, allocator);
    CHECK(result.has_value());
    if (!result.has_value()) {
        return;
    }

    std::span<std::uint32_t> pixels = (*result).pixels();
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint32_t>(i + 1) * 0x01010101u;
    }

    const std::uint64_t expected = fnv1aBytes(pixels.data(), pixels.size());
    CHECK((*result).checksum() == expected);
    CHECK((*result).checksum() == expected);
}

TEST_CASE("checksum changes when the content changes") {
    BumpAllocator allocator{65536};
    auto result = infinity::renderer::createRenderTarget(4, 4, allocator);
    CHECK(result.has_value());
    if (!result.has_value()) {
        return;
    }

    const std::uint64_t zeroChecksum = (*result).checksum();
    (*result).pixels()[0] = 0xFF000000u;

    CHECK((*result).checksum() != zeroChecksum);
}

TEST_CASE("move construction transfers ownership and empties the source") {
    BumpAllocator allocator{65536};
    const size_t bytesBefore = allocator.usedBytes();
    {
        auto created = infinity::renderer::createRenderTarget(4, 4, allocator);
        CHECK(created.has_value());
        if (!created.has_value()) {
            return;
        }

        const size_t targetBytes = allocator.usedBytes() - bytesBefore;
        CHECK(targetBytes == 4u * 4u * 4u);

        infinity::renderer::RenderTarget moved{std::move(*created)};

        CHECK(moved.width() == 4);
        CHECK(moved.height() == 4);
        CHECK((*created).width() == 0);
        CHECK((*created).height() == 0);
        CHECK((*created).pixels().empty());
        CHECK(allocator.freedBytes() == 0);
    }
    // The moved-to target releases the exact block size once (ADR-005).
    CHECK(allocator.allocationCount() == 1);
    CHECK(allocator.freedBytes() == allocator.usedBytes() - bytesBefore);
}

TEST_CASE("move assignment returns the overwritten buffer to the allocator") {
    BumpAllocator allocator{65536};
    {
        auto first = infinity::renderer::createRenderTarget(4, 4, allocator);
        auto second = infinity::renderer::createRenderTarget(8, 8, allocator);
        CHECK(first.has_value());
        CHECK(second.has_value());
        if (!first.has_value() || !second.has_value()) {
            return;
        }

        const size_t firstBytes = size_t{4} * 4u * 4u;
        const size_t usedAfterCreation = allocator.usedBytes();

        *first = std::move(*second);

        // The 4x4 buffer is released exactly once, the 8x8 buffer transferred.
        CHECK(allocator.freedBytes() == firstBytes);
        CHECK(first->width() == 8);
        CHECK(second->width() == 0);
        // The bump double never shrinks: usedBytes stays at the two allocations.
        CHECK(allocator.usedBytes() == usedAfterCreation);
    }
    CHECK(allocator.freedBytes() == allocator.usedBytes());
}

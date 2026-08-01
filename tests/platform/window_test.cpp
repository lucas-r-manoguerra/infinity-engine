// tests/platform/window_test.cpp
//
// Contract tests for the window abstraction (F3.1): the createWindow factory
// (config honored, zero-size and allocation failures reported as errors) and
// the Window lifecycle through WindowPtr/WindowDeleter (explicit destructor +
// deallocate with the exact allocation size, ADR-005). The allocator double
// mirrors tests/core/allocator_test.cpp. codeValid tests live in input_test
// (input_source.h).
#include "infinity/platform/window.h"

#include "infinity/core/allocator.h"
#include "infinity/core/error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <doctest/doctest.h>

namespace {

// Minimal Allocator double: bumps a fixed block under a byte budget and counts
// frees so tests can assert the exact release contract (allocator_test.cpp).
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

// Errors are asserted by category, never by enum equality, so doctest never
// stringifies a PlatformError operand (ADL note in tests/core/error_test.cpp).
[[nodiscard]] bool isMappedTo(infinity::platform::PlatformError code,
                              infinity::core::ErrorCategory category) noexcept {
    return infinity::platform::categoryOf(code) == category;
}

} // namespace

TEST_CASE("createWindow returns a window honoring the config") {
    BumpAllocator allocator{1024};
    const std::string_view title = "test window";
    const infinity::platform::WindowConfig config{.title = title, .width = 1280, .height = 720};

    const auto result = infinity::platform::createWindow(allocator, config);

    CHECK(result.has_value());
    if (!result.has_value()) {
        return;
    }
    CHECK((*result).get() != nullptr);
    CHECK((*result)->width() == 1280);
    CHECK((*result)->height() == 720);
    const std::string_view headless = "headless";
    CHECK((*result)->backendName() == headless);
}

TEST_CASE("createWindow rejects a zero-sized config") {
    BumpAllocator allocator{1024};
    const infinity::platform::WindowConfig zeroWidth{
        .title = std::string_view("zero"), .width = 0, .height = 600};
    const infinity::platform::WindowConfig zeroHeight{
        .title = std::string_view("zero"), .width = 800, .height = 0};

    const auto noWidth = infinity::platform::createWindow(allocator, zeroWidth);
    const auto noHeight = infinity::platform::createWindow(allocator, zeroHeight);

    CHECK_FALSE(noWidth.has_value());
    CHECK(isMappedTo(noWidth.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
    CHECK_FALSE(noHeight.has_value());
    CHECK(isMappedTo(noHeight.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
}

TEST_CASE("createWindow reports an allocation failure cleanly") {
    BumpAllocator allocator{4};

    const auto result =
        infinity::platform::createWindow(allocator, infinity::platform::WindowConfig{});

    CHECK_FALSE(result.has_value());
    CHECK(isMappedTo(result.error(), infinity::core::ErrorCategory::RESOURCE));
    CHECK(allocator.allocationCount() == 0);
}

TEST_CASE("resize updates the window size") {
    BumpAllocator allocator{1024};
    const auto result =
        infinity::platform::createWindow(allocator, infinity::platform::WindowConfig{});
    CHECK(result.has_value());
    if (!result.has_value()) {
        return;
    }

    const auto success = (*result)->resize(1920, 1080);

    CHECK(success.has_value());
    CHECK((*result)->width() == 1920);
    CHECK((*result)->height() == 1080);
}

TEST_CASE("resize rejects a zero dimension and keeps the previous size") {
    BumpAllocator allocator{1024};
    const auto result =
        infinity::platform::createWindow(allocator, infinity::platform::WindowConfig{});
    CHECK(result.has_value());
    if (!result.has_value()) {
        return;
    }

    CHECK_FALSE((*result)->resize(0, 600).has_value());
    CHECK((*result)->width() == 800);
    CHECK((*result)->height() == 600);

    CHECK_FALSE((*result)->resize(1920, 0).has_value());
    CHECK((*result)->width() == 800);
    CHECK((*result)->height() == 600);
}

TEST_CASE("requestClose is reflected by closeRequested and is idempotent") {
    BumpAllocator allocator{1024};
    const auto result =
        infinity::platform::createWindow(allocator, infinity::platform::WindowConfig{});
    CHECK(result.has_value());
    if (!result.has_value()) {
        return;
    }

    CHECK_FALSE((*result)->closeRequested());
    (*result)->requestClose();
    CHECK((*result)->closeRequested());
    (*result)->requestClose();
    CHECK((*result)->closeRequested());
}

TEST_CASE("pollEvents is a safe no-op for the headless backend") {
    BumpAllocator allocator{1024};
    const auto result =
        infinity::platform::createWindow(allocator, infinity::platform::WindowConfig{});
    CHECK(result.has_value());
    if (!result.has_value()) {
        return;
    }

    (*result)->pollEvents();
    (*result)->pollEvents();

    CHECK_FALSE((*result)->closeRequested());
}

TEST_CASE("the window is released to its allocator on destruction") {
    BumpAllocator allocator{1024};
    const size_t bytesBefore = allocator.usedBytes();
    const size_t allocationsBefore = allocator.allocationCount();

    {
        const auto result =
            infinity::platform::createWindow(allocator, infinity::platform::WindowConfig{});
        CHECK(result.has_value());
        if (!result.has_value()) {
            return;
        }
        const size_t windowBytes = allocator.usedBytes() - bytesBefore;
        CHECK(windowBytes > 0);
        CHECK(allocator.freedBytes() == 0);
    }

    // The deleter must deallocate the exact allocation size: a wrong size
    // (e.g. sizeof(Window)) would leave freedBytes below the block size.
    CHECK(allocator.allocationCount() == allocationsBefore + 1);
    CHECK(allocator.freedBytes() == allocator.usedBytes() - bytesBefore);
}

// tests/platform/platform_test.cpp
//
// Contract tests for the platform context (F3.4, rule 03/05): Platform owns
// the window, queue and action map, init() creates the window through the
// provided allocator, a second init is rejected, and destruction releases
// everything back to the allocator. No globals, no singletons: every instance
// is rooted in an explicit Allocator (ADR-005).
#include "infinity/platform/platform.h"

#include "infinity/core/allocator.h"
#include "infinity/core/error.h"

#include <array>
#include <cstddef>
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

TEST_CASE("a fresh Platform is not initialized and exposes its default state") {
    BumpAllocator allocator{1024};
    infinity::platform::Platform platform{allocator};

    CHECK_FALSE(platform.isInitialized());
    CHECK(platform.inputQueue().size() == 0);
    CHECK(platform.actionMap().boundCount() == 0);
}

TEST_CASE("init builds the platform from the config") {
    BumpAllocator allocator{1024};
    infinity::platform::Platform platform{allocator};
    const std::string_view title = "platform test";
    const infinity::platform::PlatformConfig config{
        .window = infinity::platform::WindowConfig{.title = title, .width = 1024, .height = 768}};

    const auto result = platform.init(config);

    CHECK(result.has_value());
    CHECK(platform.isInitialized());
    CHECK(platform.window().width() == 1024);
    CHECK(platform.window().height() == 768);
}

TEST_CASE("a second init is rejected") {
    BumpAllocator allocator{1024};
    infinity::platform::Platform platform{allocator};

    const auto first = platform.init(infinity::platform::PlatformConfig{});
    const auto second = platform.init(infinity::platform::PlatformConfig{});

    CHECK(first.has_value());
    CHECK_FALSE(second.has_value());
    CHECK(isMappedTo(second.error(), infinity::core::ErrorCategory::INVALID_STATE));
    CHECK(platform.isInitialized());
}

TEST_CASE("a failed init leaves the platform uninitialized and reusable") {
    BumpAllocator allocator{4};
    infinity::platform::Platform platform{allocator};

    const auto failed = platform.init(infinity::platform::PlatformConfig{});

    CHECK_FALSE(failed.has_value());
    CHECK(isMappedTo(failed.error(), infinity::core::ErrorCategory::RESOURCE));
    CHECK_FALSE(platform.isInitialized());
}

TEST_CASE("pollEvents on an uninitialized platform is a safe no-op") {
    BumpAllocator allocator{1024};
    infinity::platform::Platform platform{allocator};

    platform.pollEvents();

    CHECK_FALSE(platform.isInitialized());
}

TEST_CASE("pollEvents drains the active window without error") {
    BumpAllocator allocator{1024};
    infinity::platform::Platform platform{allocator};
    const auto init = platform.init(infinity::platform::PlatformConfig{});
    CHECK(init.has_value());
    if (!init.has_value()) {
        return;
    }

    platform.pollEvents();
    platform.pollEvents();

    CHECK_FALSE(platform.window().closeRequested());
}

TEST_CASE("the input queue is reachable and usable through the platform") {
    BumpAllocator allocator{1024};
    infinity::platform::Platform platform{allocator};
    const auto init = platform.init(infinity::platform::PlatformConfig{});
    CHECK(init.has_value());
    if (!init.has_value()) {
        return;
    }

    const auto pushed = platform.inputQueue().push(infinity::platform::InputSource::KEY, 1, 1.0f);

    CHECK(pushed.has_value());
    CHECK(platform.inputQueue().size() == 1);
}

TEST_CASE("the action map is reachable and usable through the platform") {
    BumpAllocator allocator{1024};
    infinity::platform::Platform platform{allocator};
    const auto init = platform.init(infinity::platform::PlatformConfig{});
    CHECK(init.has_value());
    if (!init.has_value()) {
        return;
    }

    const auto bound = platform.actionMap().bind(1, infinity::platform::InputSource::KEY, 2);

    CHECK(bound.has_value());
    CHECK(platform.actionMap().boundCount() == 1);
    const auto resolved = platform.actionMap().resolve(infinity::platform::InputSource::KEY, 2);
    CHECK(resolved.has_value());
    if (!resolved.has_value()) {
        return;
    }
    CHECK(*resolved == 1);
}

TEST_CASE("destroying the platform releases the window to its allocator") {
    BumpAllocator allocator{1024};
    const size_t bytesBefore = allocator.usedBytes();
    const size_t allocationsBefore = allocator.allocationCount();

    {
        infinity::platform::Platform platform{allocator};
        const auto init = platform.init(infinity::platform::PlatformConfig{});
        CHECK(init.has_value());
        if (!init.has_value()) {
            return;
        }
        CHECK(allocator.allocationCount() == allocationsBefore + 1);
        CHECK(allocator.freedBytes() == 0);
    }

    CHECK(allocator.freedBytes() == allocator.usedBytes() - bytesBefore);
}

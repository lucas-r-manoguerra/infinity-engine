// tests/platform/window_test.cpp
//
// Contract tests for the window abstraction (F3.1): the createWindow factory
// (config honored, zero-size and allocation failures reported as errors) and
// the Window lifecycle through WindowPtr/WindowDeleter (explicit destructor +
// deallocate with the exact allocation size, ADR-005). The allocator double
// mirrors tests/core/allocator_test.cpp. codeValid tests live in input_test
// (input_source.h).
//
// Backend awareness: the same binary runs on the X11 backend (a real native
// surface, needs a reachable display server) and on the headless backend (no
// surface, always works). Cases that need a surface skip cleanly when none
// could be created - the X11 backend reports UNSUPPORTED on machines without
// DISPLAY, which is the correct outcome there and must never fail CI.
#include "infinity/platform/window.h"

#include "infinity/core/allocator.h"
#include "infinity/core/error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <string_view>
#include <utility>

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

// True when a display server is reachable (DISPLAY set). The X11 backend needs
// one to build a surface; the headless backend never does.
[[nodiscard]] bool hasDisplay() noexcept {
    // getenv is not thread-safe; tests run single-threaded under doctest and
    // DISPLAY is set once by the harness before the binary starts.
    return std::getenv("DISPLAY") != nullptr; // NOLINT(concurrency-mt-unsafe)
}
// Attempts to create a window, returning a null WindowPtr when no surface
// could be built. On the X11 backend without a reachable display server
// createWindow() correctly reports UNSUPPORTED; the factory error contract is
// covered by the dedicated cases below, so lifecycle cases just skip.
[[nodiscard]] infinity::platform::WindowPtr
makeWindow(BumpAllocator& allocator, const infinity::platform::WindowConfig& config) {
    auto result = infinity::platform::createWindow(allocator, config);
    if (!result.has_value()) {
        return nullptr;
    }
    return std::move(*result);
}

} // namespace

TEST_CASE("createWindow returns a window honoring the config") {
    BumpAllocator allocator{1024};
    const std::string_view title = "test window";
    const infinity::platform::WindowConfig config{.title = title, .width = 1280, .height = 720};

    const auto result = infinity::platform::createWindow(allocator, config);

    if (!result.has_value()) {
        // The X11 backend reports UNSUPPORTED (NOT_SUPPORTED) on a machine
        // without a reachable display server, before touching the allocator;
        // the headless backend never fails here. Accepting UNSUPPORTED only
        // when no display is present keeps this green on X11-without-X,
        // X11-under-Xvfb and headless-only boxes.
        CHECK(hasDisplay() == false);
        CHECK(isMappedTo(result.error(), infinity::core::ErrorCategory::NOT_SUPPORTED));
        return;
    }

    CHECK((*result).get() != nullptr);
    CHECK((*result)->width() == 1280);
    CHECK((*result)->height() == 720);
    const std::string_view name = (*result)->backendName();
    const bool isX11 = name == std::string_view("x11");
    if (isX11) {
        // A real X11 surface implies a display server was reachable.
        CHECK(hasDisplay());
    } else {
        CHECK(name == std::string_view("headless"));
    }
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
    if (result.has_value()) {
        return;
    }
    // A too-small budget is ALLOCATION_FAILED (RESOURCE). On the X11 backend
    // without a reachable display the factory fails earlier with UNSUPPORTED
    // (NOT_SUPPORTED), before touching the allocator. Both are the correct
    // outcome for their environment.
    const infinity::core::ErrorCategory category = infinity::platform::categoryOf(result.error());
    const bool isExpectedFailure = category == infinity::core::ErrorCategory::RESOURCE ||
                                   category == infinity::core::ErrorCategory::NOT_SUPPORTED;
    CHECK(isExpectedFailure);
    if (category == infinity::core::ErrorCategory::RESOURCE) {
        CHECK(allocator.allocationCount() == 0);
    }
}

TEST_CASE("resize updates the window size") {
    BumpAllocator allocator{1024};
    auto window = makeWindow(allocator, infinity::platform::WindowConfig{});
    if (window == nullptr) {
        return; // no reachable display server: nothing to exercise
    }

    const auto success = window->resize(1920, 1080);

    CHECK(success.has_value());
    CHECK(window->width() == 1920);
    CHECK(window->height() == 1080);
}

TEST_CASE("resize rejects a zero dimension and keeps the previous size") {
    BumpAllocator allocator{1024};
    auto window = makeWindow(allocator, infinity::platform::WindowConfig{});
    if (window == nullptr) {
        return; // no reachable display server: nothing to exercise
    }

    CHECK_FALSE(window->resize(0, 600).has_value());
    CHECK(window->width() == 800);
    CHECK(window->height() == 600);

    CHECK_FALSE(window->resize(1920, 0).has_value());
    CHECK(window->width() == 800);
    CHECK(window->height() == 600);
}

TEST_CASE("requestClose is reflected by closeRequested and is idempotent") {
    BumpAllocator allocator{1024};
    auto window = makeWindow(allocator, infinity::platform::WindowConfig{});
    if (window == nullptr) {
        return; // no reachable display server: nothing to exercise
    }

    CHECK_FALSE(window->closeRequested());
    window->requestClose();
    CHECK(window->closeRequested());
    window->requestClose();
    CHECK(window->closeRequested());
}

TEST_CASE("pollEvents is safe when there are no events") {
    BumpAllocator allocator{1024};
    auto window = makeWindow(allocator, infinity::platform::WindowConfig{});
    if (window == nullptr) {
        return; // no reachable display server: nothing to exercise
    }

    window->pollEvents();
    window->pollEvents();

    CHECK_FALSE(window->closeRequested());
}

TEST_CASE("the window is released to its allocator on destruction") {
    BumpAllocator allocator{1024};
    const size_t bytesBefore = allocator.usedBytes();
    const size_t allocationsBefore = allocator.allocationCount();

    {
        auto window = makeWindow(allocator, infinity::platform::WindowConfig{});
        if (window == nullptr) {
            return; // no reachable display server: nothing was allocated
        }
        const size_t windowBytes = allocator.usedBytes() - bytesBefore;
        CHECK(windowBytes > 0);
        // The window's block stays live while the window is alive. The X11
        // backend also copies the title through the allocator (already
        // released at this point), so only liveness, not a zero free count,
        // is asserted here.
        CHECK(allocator.usedBytes() - allocator.freedBytes() > 0);
    }

    // The deleter must deallocate the exact allocation sizes: a wrong size
    // (e.g. sizeof(Window)) would leave freedBytes below the used bytes. This
    // holds for the headless backend (one block) and the X11 backend (window
    // block plus the transient title copy, both released).
    CHECK(allocator.allocationCount() >= allocationsBefore + 1);
    CHECK(allocator.freedBytes() == allocator.usedBytes() - bytesBefore);
}

// X11 smoke coverage (F3.1). These exercise a real native surface, so they
// run only when a display server is reachable AND the compiled backend is
// X11; otherwise they skip cleanly. The devcontainer runs them under
// xvfb-run; CI machines without DISPLAY never fail on them.
TEST_CASE("x11 window smoke: size, close state, resize and event draining") {
    if (!hasDisplay()) {
        return; // no display server: nothing real to smoke-test
    }
    BumpAllocator allocator{1024};
    auto window = makeWindow(allocator, infinity::platform::WindowConfig{});
    if (window == nullptr) {
        return; // already covered by the factory cases above
    }
    if (window->backendName() != "x11") {
        return; // headless build with DISPLAY set: no native surface to smoke
    }

    CHECK(window->width() == 800);
    CHECK(window->height() == 600);

    // pollEvents is a no-op-safe drain: no events mean no state change.
    window->pollEvents();
    CHECK_FALSE(window->closeRequested());

    // A resize round-trips through XResizeWindow and updates the stored size.
    const auto resized = window->resize(1024, 768);
    CHECK(resized.has_value());
    CHECK(window->width() == 1024);
    CHECK(window->height() == 768);

    // requestClose flips the close flag; it survives a pollEvents drain.
    window->requestClose();
    window->pollEvents();
    CHECK(window->closeRequested());
}

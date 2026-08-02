// infinity/platform/src/window.cpp
#include "infinity/platform/window.h"

#include "headless/headless_window.h"

#if defined(INFINITY_PLATFORM_X11)
#include "x11/x11_window.h"
#endif

#include <new>
#include <utility>

namespace infinity::platform {

namespace {

// Constructs a backend of type Backend in the allocator and wraps it in a
// WindowPtr whose deleter releases the block back to the same allocator with
// the exact allocation size (ADR-005): ownership and size never drift.
template <typename Backend>
Expected<WindowPtr> makeBackend(core::Allocator& allocator, const WindowConfig& config) noexcept {
    void* block = allocator.allocate(sizeof(Backend), alignof(Backend));
    if (block == nullptr) {
        return std::unexpected(PlatformError::ALLOCATION_FAILED);
    }
    auto* window = new (block) Backend(config);
    return WindowPtr(window, WindowDeleter{.allocator = &allocator, .size = sizeof(Backend)});
}

} // namespace

Expected<WindowPtr> createWindow(core::Allocator& allocator, const WindowConfig& config) noexcept {
    if (config.width == 0 || config.height == 0) {
        return std::unexpected(PlatformError::INVALID_SIZE);
    }
#if defined(INFINITY_PLATFORM_X11)
    // X11 backend (F3.1): a real native surface when a display server is
    // reachable; UNSUPPORTED otherwise (createX11Window owns the failure path).
    return createX11Window(allocator, config);
#else
    // INFINITY_PLATFORM_HEADLESS is defined in engine/platform/CMakeLists.txt
    // when this branch is the active backend.
    return makeBackend<HeadlessWindow>(allocator, config);
#endif
}

} // namespace infinity::platform

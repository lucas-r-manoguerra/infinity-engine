// infinity/platform/src/platform.cpp
#include "infinity/platform/platform.h"

#include <cassert>
#include <utility>

namespace infinity::platform {

Platform::Platform(core::Allocator& allocator) noexcept : m_allocator(allocator) {}

ExpectedVoid Platform::init(const PlatformConfig& config) noexcept {
    if (m_initialized) {
        return std::unexpected(PlatformError::ALREADY_INITIALIZED);
    }
    Expected<WindowPtr> window = createWindow(m_allocator, config.window);
    if (!window) {
        return std::unexpected(window.error());
    }
    m_window = std::move(*window);
    m_initialized = true;
    return {};
}

void Platform::pollEvents() noexcept {
    if (m_initialized) {
        m_window->pollEvents();
    }
}

Window& Platform::window() noexcept {
    // Programming error (ADR-003): callers must init() before touching the
    // window. Asserted in debug; release behavior is undefined by contract.
    assert(m_initialized);
    return *m_window;
}

InputQueue& Platform::inputQueue() noexcept { return m_queue; }

ActionMap& Platform::actionMap() noexcept { return m_actions; }

bool Platform::isInitialized() const noexcept { return m_initialized; }

} // namespace infinity::platform

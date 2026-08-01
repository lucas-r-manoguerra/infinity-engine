// infinity/platform/src/headless/headless_window.cpp
#include "headless/headless_window.h"

#include "infinity/platform/error.h"

#include <cstdint>
#include <string_view>

namespace infinity::platform {

HeadlessWindow::HeadlessWindow(const WindowConfig& config) noexcept
    : m_width(config.width), m_height(config.height) {}

std::uint32_t HeadlessWindow::width() const noexcept { return m_width; }

std::uint32_t HeadlessWindow::height() const noexcept { return m_height; }

ExpectedVoid HeadlessWindow::resize(std::uint32_t newWidth, std::uint32_t newHeight) noexcept {
    if (newWidth == 0 || newHeight == 0) {
        return std::unexpected(PlatformError::INVALID_SIZE);
    }
    m_width = newWidth;
    m_height = newHeight;
    return {};
}

void HeadlessWindow::requestClose() noexcept { m_closeRequested = true; }

bool HeadlessWindow::closeRequested() const noexcept { return m_closeRequested; }

void HeadlessWindow::pollEvents() noexcept {
    // The headless backend has no platform events to drain (ADR-030).
}

std::string_view HeadlessWindow::backendName() const noexcept { return "headless"; }

} // namespace infinity::platform

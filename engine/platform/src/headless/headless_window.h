// infinity/platform/src/headless/headless_window.h
//
// Headless window backend (F3.1, ADR-030): stores size and close state only,
// with no native surface and no events. It exists so the renderer, ECS and CI
// run hermetically before X11 ships. Selected by createWindow() when
// INFINITY_PLATFORM_X11 is not defined.
#pragma once

#include "infinity/platform/window.h"

#include <cstdint>
#include <string_view>

namespace infinity::platform {

class HeadlessWindow final : public Window {
public:
    explicit HeadlessWindow(const WindowConfig& config) noexcept;

    [[nodiscard]] std::uint32_t width() const noexcept override;
    [[nodiscard]] std::uint32_t height() const noexcept override;
    [[nodiscard]] ExpectedVoid resize(std::uint32_t newWidth,
                                      std::uint32_t newHeight) noexcept override;
    void requestClose() noexcept override;
    [[nodiscard]] bool closeRequested() const noexcept override;
    void pollEvents() noexcept override;
    [[nodiscard]] std::string_view backendName() const noexcept override;

private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    bool m_closeRequested = false;
};

} // namespace infinity::platform

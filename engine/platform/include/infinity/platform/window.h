// infinity/platform/window.h
//
// Window abstraction (F3.1, rule 01). The renderer and the runtime talk to
// this interface only; concrete backends live in src/<backend>/ and are
// selected at compile time by createWindow().
//
//   Ownership  - A window is always owned through WindowPtr, a unique_ptr
//                whose deleter releases the backend back to the allocator
//                that created it (ADR-005): no bare delete, no leaked block,
//                and the exact allocation size is what gets deallocated.
//   Backends   - createWindow() picks the backend at compile time. Today the
//                headless backend (ADR-030) is wired: it stores size and
//                close state and never opens a native surface, so the
//                renderer, ECS and CI run hermetically before X11 lands
//                (F3.1). The X11 factory branch reports UNSUPPORTED until the
//                backend ships.
#pragma once

#include "infinity/core/allocator.h"
#include "infinity/core/error.h"
#include "infinity/platform/error.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace infinity::platform {

// Configuration for createWindow(). title is a string_view the caller owns:
// it is not copied, so the underlying buffer must outlive the window.
struct WindowConfig {
    std::string_view title = "Infinity Engine";
    std::uint32_t width = 800;
    std::uint32_t height = 600;
};

// Pure window interface. Backends implement the exact same contract so
// swapping X11 for headless (and back) never touches callers (rule 01).
class Window {
public:
    virtual ~Window() = default;

    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;

    // Rejects zero dimensions (INVALID_SIZE); on error the window keeps its
    // previous size.
    [[nodiscard]] virtual ExpectedVoid resize(std::uint32_t newWidth,
                                              std::uint32_t newHeight) noexcept = 0;

    virtual void requestClose() noexcept = 0;
    [[nodiscard]] virtual bool closeRequested() const noexcept = 0;

    // Drains platform events into the input queue and close state. The
    // headless backend has no events to drain (no-op).
    virtual void pollEvents() noexcept = 0;

    [[nodiscard]] virtual std::string_view backendName() const noexcept = 0;
};

// Releases a Window back to its owning allocator: explicit destructor call
// followed by deallocate with the exact size used at creation (ADR-005,
// allocator.h). Defined inline because unique_ptr must call it from every TU
// that destroys a WindowPtr.
struct WindowDeleter {
    core::Allocator* allocator = nullptr;
    std::size_t size = 0;

    void operator()(Window* window) const noexcept {
        if (window == nullptr) {
            return;
        }
        window->~Window();
        allocator->deallocate(window, size);
    }
};

using WindowPtr = std::unique_ptr<Window, WindowDeleter>;

// Creates a window through allocator with the given config (rule 04): the
// caller owns the returned window and it is released back to the same
// allocator when the WindowPtr goes out of scope. Fails with INVALID_SIZE on
// a zero dimension and ALLOCATION_FAILED when the allocator cannot satisfy
// the request. The backend is chosen at compile time.
[[nodiscard]] Expected<WindowPtr> createWindow(core::Allocator& allocator,
                                               const WindowConfig& config) noexcept;

} // namespace infinity::platform

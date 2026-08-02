// infinity/platform/src/x11/x11_window.h
//
// X11 window backend (F3.1): a real native surface on top of Xlib, selected by
// createWindow() when the X11 backend is compiled (engine/platform/
// CMakeLists.txt). Mirrors the HeadlessWindow contract exactly, so swapping
// backends never touches callers (rule 01).
//
//   X11 types - This header stays free of <X11/Xlib.h> (rule 02): the display
//               and the surface are opaque handles, forward-declared below
//               with the exact declarations Xlib uses, so the .cpp can include
//               Xlib.h afterwards without any redefinition conflict.
//   Ownership  - The display is opened by createX11Window() and handed to the
//               window, which owns it from construction (RAII, rule 03): the
//               destructor releases the native surface and then the
//               connection, in that order, on every path.
#pragma once

#include "infinity/platform/window.h"

#include <cstdint>
#include <string_view>

// Opaque X11 handles, forward-declared so this header never pulls in Xlib.
// The display is a struct and ::Window is an XID (unsigned long). Xlib's own
// typedefs (_XDisplay, XID) are reserved identifiers and never appear here:
// the .cpp converts X11Display* to Display* with a reinterpret_cast
// (round-trip pointer cast, well-defined, C++11 5.2.10).
struct X11Display;
using Window = unsigned long;

namespace infinity::platform {

// Real X11 surface backend. The factory (createX11Window) opens the display,
// builds the native window and owns the failure path; this constructor is
// trivial and cannot fail.
class X11Window final : public Window {
public:
    X11Window(X11Display* display, const WindowConfig& config) noexcept;
    ~X11Window() override;

    [[nodiscard]] std::uint32_t width() const noexcept override;
    [[nodiscard]] std::uint32_t height() const noexcept override;
    [[nodiscard]] ExpectedVoid resize(std::uint32_t newWidth,
                                      std::uint32_t newHeight) noexcept override;
    void requestClose() noexcept override;
    [[nodiscard]] bool closeRequested() const noexcept override;
    void pollEvents() noexcept override;
    [[nodiscard]] std::string_view backendName() const noexcept override;

private:
    // The factory builds the native surface and the WM protocol atoms right
    // after construction; friendship keeps those one-time initializers
    // private (rule 02 allows friend for a justified factory).
    friend Expected<WindowPtr> createX11Window(core::Allocator& allocator,
                                               const WindowConfig& config) noexcept;

    X11Display* m_display;
    ::Window m_window = 0;
    std::uint32_t m_width;
    std::uint32_t m_height;
    bool m_closeRequested = false;
    bool m_ownsDisplay = true;
    // WM protocol atoms (X11 Atom is unsigned long). A close request arrives
    // as a ClientMessage whose message_type is WM_PROTOCOLS and whose first
    // payload slot carries WM_DELETE_WINDOW.
    unsigned long m_wmProtocols = 0;
    long m_wmDeleteMessage = 0;
};

// Creates a real X11 surface through the allocator. Fails with UNSUPPORTED
// when no display server is reachable (DISPLAY unset or XOpenDisplay failed)
// and with ALLOCATION_FAILED when the allocator cannot satisfy the request.
// Any native creation failure releases the partially-built window before
// returning: never a leak, never a half-open surface (rule 03).
[[nodiscard]] Expected<WindowPtr> createX11Window(core::Allocator& allocator,
                                                  const WindowConfig& config) noexcept;

} // namespace infinity::platform

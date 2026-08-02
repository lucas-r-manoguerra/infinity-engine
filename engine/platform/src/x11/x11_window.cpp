// infinity/platform/src/x11/x11_window.cpp
#include "x11/x11_window.h"

#include "infinity/platform/error.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

namespace infinity::platform {

namespace {

// The header keeps Xlib out (rule 02) with an opaque X11Display*; the
// concrete Display* is restored here (round-trip pointer cast, well-defined).
[[nodiscard]] Display* nativeDisplay(X11Display* display) noexcept {
    return reinterpret_cast<Display*>(display);
}

// Installed only while the native surface is created: swallows asynchronous
// protocol errors (e.g. BadAlloc) so a server-side failure surfaces as a
// clean std::expected instead of aborting through Xlib's default handler.
// Stateless (rule 11): the caller restores the previous handler from the
// local XSetErrorHandler return value.
int swallowXError(Display* display, XErrorEvent* event) noexcept {
    (void)display;
    (void)event;
    return 0;
}

// Event mask every window requests: exposure, keyboard, pointer, buttons and
// structure (configure/map) notifications. WM_DELETE_WINDOW arrives as a
// ClientMessage on top of these.
unsigned long windowEventMask() noexcept {
    return ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask | ButtonPressMask |
           ButtonReleaseMask | PointerMotionMask;
}

} // namespace

X11Window::X11Window(X11Display* display, const WindowConfig& config) noexcept
    : m_display(display), m_width(config.width), m_height(config.height) {}

X11Window::~X11Window() {
    // RAII (rule 03): the native surface is destroyed before the connection
    // that owns it. The display is closed only when this window owns it - the
    // factory always opens the display it hands over, so every constructed
    // window owns it.
    if (m_window != 0) {
        XDestroyWindow(nativeDisplay(m_display), m_window);
    }
    if (m_ownsDisplay) {
        XCloseDisplay(nativeDisplay(m_display));
    }
}

std::uint32_t X11Window::width() const noexcept { return m_width; }

std::uint32_t X11Window::height() const noexcept { return m_height; }

ExpectedVoid X11Window::resize(std::uint32_t newWidth, std::uint32_t newHeight) noexcept {
    if (newWidth == 0 || newHeight == 0) {
        return std::unexpected(PlatformError::INVALID_SIZE);
    }
    m_width = newWidth;
    m_height = newHeight;
    if (m_window != 0) {
        XResizeWindow(nativeDisplay(m_display), m_window, newWidth, newHeight);
        XFlush(nativeDisplay(m_display));
    }
    return {};
}

void X11Window::requestClose() noexcept { m_closeRequested = true; }

bool X11Window::closeRequested() const noexcept { return m_closeRequested; }

void X11Window::pollEvents() noexcept {
    Display* display = nativeDisplay(m_display);
    while (XPending(display) > 0) {
        XEvent event{};
        XNextEvent(display, &event);
        switch (event.type) {
        case ClientMessage:
            // A window-manager close arrives as a ClientMessage whose
            // message_type is WM_PROTOCOLS and whose first payload slot is the
            // WM_DELETE_WINDOW atom.
            if (event.xclient.message_type == m_wmProtocols &&
                event.xclient.data.l[0] == m_wmDeleteMessage) {
                requestClose();
            }
            break;
        case ConfigureNotify:
            m_width = static_cast<std::uint32_t>(event.xconfigure.width);
            m_height = static_cast<std::uint32_t>(event.xconfigure.height);
            break;
        default:
            break;
        }
    }
}

std::string_view X11Window::backendName() const noexcept { return "x11"; }

Expected<WindowPtr> createX11Window(core::Allocator& allocator,
                                    const WindowConfig& config) noexcept {
    // The X server is reached through the DISPLAY environment variable. A
    // missing or broken connection cannot produce a surface, so the capability
    // is reported as unsupported (rule 04) - no new error code is needed.
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return std::unexpected(PlatformError::UNSUPPORTED);
    }

    void* block = allocator.allocate(sizeof(X11Window), alignof(X11Window));
    if (block == nullptr) {
        XCloseDisplay(display);
        return std::unexpected(PlatformError::ALLOCATION_FAILED);
    }
    auto* window = new (block) X11Window(reinterpret_cast<X11Display*>(display), config);

    // Swallow asynchronous protocol errors while the surface is built, so a
    // server-side failure (e.g. BadAlloc) never aborts the process (Xlib's
    // default handler exits). The previous handler is restored from the local
    // XSetErrorHandler return value: no global state (rule 11).
    int (*previousHandler)(Display*, XErrorEvent*) = XSetErrorHandler(&swallowXError);

    // Common teardown when a native step fails: drain pending errors while
    // the swallow handler is installed, restore the previous one, then let
    // the destructor release the surface and the display (RAII, rule 03) and
    // return the allocator block.
    const auto failWith = [&](PlatformError error) -> Expected<WindowPtr> {
        XSync(display, False);
        XSetErrorHandler(previousHandler);
        window->~X11Window();
        allocator.deallocate(block, sizeof(X11Window));
        return std::unexpected(error);
    };

    const int screen = DefaultScreen(display);
    const ::Window root = RootWindow(display, screen);
    const unsigned long blackPixel = BlackPixel(display, screen);
    const unsigned long whitePixel = WhitePixel(display, screen);

    window->m_window = XCreateSimpleWindow(display, root, 0, 0, config.width, config.height, 0,
                                           blackPixel, whitePixel);
    if (window->m_window == 0) {
        return failWith(PlatformError::UNSUPPORTED);
    }

    // Without XSelectInput no events are delivered to the window.
    if (XSelectInput(display, window->m_window, windowEventMask()) == 0) {
        return failWith(PlatformError::UNSUPPORTED);
    }

    // Ask the WM to deliver WM_DELETE_WINDOW as a ClientMessage so the close
    // button maps to requestClose() instead of killing the connection. Both
    // atoms must be remembered to interpret pollEvents().
    window->m_wmProtocols = XInternAtom(display, "WM_PROTOCOLS", False);
    // Atoms are small positive values; storing the delete atom as signed long
    // keeps the pollEvents comparison with XClientMessageEvent.data.l[0]
    // (long) sign-clean (clang-tidy, rule 05).
    window->m_wmDeleteMessage = static_cast<long>(XInternAtom(display, "WM_DELETE_WINDOW", False));
    std::array<Atom, 1> deleteProtocol{static_cast<Atom>(window->m_wmDeleteMessage)};
    XSetWMProtocols(display, window->m_window, deleteProtocol.data(), 1);

    // Window title: the classic WM_NAME property (XStoreName) plus the UTF-8
    // _NET_WM_NAME property (XSetWMName) for modern window managers.
    XTextProperty name{};
    name.value = reinterpret_cast<unsigned char*>(const_cast<char*>(config.title.data()));
    name.encoding = XInternAtom(display, "UTF8_STRING", False);
    name.format = 8;
    name.nitems = config.title.size();
    XSetWMName(display, window->m_window, &name);

    // XStoreName needs a NUL-terminated C string; the caller-owned title is a
    // string_view, so copy it through the module allocator and release it
    // immediately (creation is not a hot path, rule 08).
    char* titleCString = static_cast<char*>(allocator.allocate(config.title.size() + 1, 1));
    if (titleCString == nullptr) {
        return failWith(PlatformError::ALLOCATION_FAILED);
    }
    std::memcpy(titleCString, config.title.data(), config.title.size());
    titleCString[config.title.size()] = '\0';
    XStoreName(display, window->m_window, titleCString);
    allocator.deallocate(titleCString, config.title.size() + 1);

    XMapWindow(display, window->m_window);
    XFlush(display);
    // Deliver any pending async errors to the swallow handler before the
    // default one is restored, so a late failure cannot abort afterwards.
    XSync(display, False);
    XSetErrorHandler(previousHandler);

    return WindowPtr(window, WindowDeleter{.allocator = &allocator, .size = sizeof(X11Window)});
}

} // namespace infinity::platform

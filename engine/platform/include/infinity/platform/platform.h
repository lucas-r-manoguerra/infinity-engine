// infinity/platform/platform.h
//
// Platform context (F3.4, ADR-033). Owns the window, the deterministic input
// queue and the action map for one engine instance. RAII: the window is
// released to its allocator in the destructor, and init() is the only
// fallible step, so callers can never hold a half-created context.
#pragma once

#include "infinity/core/allocator.h"
#include "infinity/platform/error.h"
#include "infinity/platform/input.h"
#include "infinity/platform/window.h"

namespace infinity::platform {

// Configuration for one Platform instance.
struct PlatformConfig {
    WindowConfig window;
};

class Platform {
public:
    explicit Platform(core::Allocator& allocator) noexcept;
    ~Platform() = default;

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    Platform(Platform&&) = delete;
    Platform& operator=(Platform&&) = delete;

    // Creates the window. A second init reports ALREADY_INITIALIZED; the
    // window factory error is forwarded untouched (rule 04: translate at
    // module boundaries, close at the system boundary).
    [[nodiscard]] ExpectedVoid init(const PlatformConfig& config) noexcept;

    // Drains platform events into the queue and close state. A no-op before
    // init (there is nothing to drain).
    void pollEvents() noexcept;

    // The live window. Calling this before init() is a programming error
    // (assert in debug, ADR-003).
    [[nodiscard]] Window& window() noexcept;

    // The input queue and action map are members, so they are always valid,
    // before and after init().
    [[nodiscard]] InputQueue& inputQueue() noexcept;
    [[nodiscard]] ActionMap& actionMap() noexcept;
    [[nodiscard]] bool isInitialized() const noexcept;

private:
    core::Allocator& m_allocator;
    WindowPtr m_window;
    InputQueue m_queue;
    ActionMap m_actions;
    bool m_initialized = false;
};

} // namespace infinity::platform

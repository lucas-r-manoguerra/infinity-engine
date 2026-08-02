// infinity/renderer/error.h
//
// Renderer error vocabulary (F4.1, ADR-003, rule 04). The renderer subsystem
// reuses core's ErrorCategory so category mapping and logging stay uniform
// across the engine (engine/core/.../error.h, F2.3). The model is identical
// to core and platform: recoverable failures travel through
// std::expected<T, RenderError>, programming errors are assert/panic, and
// categoryOf()/toString() are total so a corrupted code can never crash nor
// yield undefined behavior.
#pragma once

#include "infinity/core/error.h"

#include <cstdint>
#include <expected>
#include <string_view>

namespace infinity::renderer {

// Renderer subsystem error codes. Values are stable: they may be serialized
// across sessions, so existing codes never change meaning and new codes are
// appended. Each code maps to exactly one core::ErrorCategory.
enum class RenderError : uint8_t {
    INVALID_ARGUMENT = 0, ///< invalid_argument - a caller-supplied argument is not usable
    INVALID_SIZE,         ///< invalid_argument - a dimension or count is zero or overflowed
    INVALID_STATE,        ///< invalid_state - operation requires a state the renderer is not in
    NOT_SUPPORTED,        ///< not_supported - a capability the backend does not provide
    ALLOCATION_FAILED,    ///< resource - the allocator could not satisfy a request
    INTERNAL,             ///< invalid_state - an internal invariant broke (never expected)
};

// Maps a RenderError to its category. Constexpr so mappings are verifiable at
// compile time. Total: out-of-range codes (corrupted values) map to
// invalid_state instead of undefined behavior.
[[nodiscard]] constexpr core::ErrorCategory categoryOf(RenderError code) noexcept {
    switch (code) {
    case RenderError::INVALID_ARGUMENT:
    case RenderError::INVALID_SIZE:
        return core::ErrorCategory::INVALID_ARGUMENT;
    case RenderError::INVALID_STATE:
    case RenderError::INTERNAL:
        return core::ErrorCategory::INVALID_STATE;
    case RenderError::NOT_SUPPORTED:
        return core::ErrorCategory::NOT_SUPPORTED;
    case RenderError::ALLOCATION_FAILED:
        return core::ErrorCategory::RESOURCE;
    }
    return core::ErrorCategory::INVALID_STATE;
}

// Human-readable name of a RenderError ("invalid_size"). Stable and meant for
// logging and diagnostics, never parsed (rule 04). Out-of-range codes return
// "unknown".
[[nodiscard]] constexpr std::string_view toString(RenderError code) noexcept {
    switch (code) {
    case RenderError::INVALID_ARGUMENT:
        return "invalid_argument";
    case RenderError::INVALID_SIZE:
        return "invalid_size";
    case RenderError::INVALID_STATE:
        return "invalid_state";
    case RenderError::NOT_SUPPORTED:
        return "not_supported";
    case RenderError::ALLOCATION_FAILED:
        return "allocation_failed";
    case RenderError::INTERNAL:
        return "internal";
    }
    return "unknown";
}

// Expected alias for renderer errors.
template <typename T> using Expected = std::expected<T, RenderError>;

// Void convenience alias for operations that only signal success or failure.
using ExpectedVoid = std::expected<void, RenderError>;

} // namespace infinity::renderer

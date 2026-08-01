// infinity/platform/error.h
//
// Platform error vocabulary (F3, ADR-003, rule 04). The platform subsystem
// reuses core's ErrorCategory so category mapping and logging stay uniform
// across the engine (engine/core/.../error.h, F2.3). The model is identical
// to core: recoverable failures travel through std::expected<T, PlatformError>,
// programming errors are assert/panic, and categoryOf()/toString() are total
// so a corrupted code can never crash nor yield undefined behavior.
#pragma once

#include "infinity/core/error.h"

#include <cstdint>
#include <expected>
#include <string_view>

namespace infinity::platform {

// Platform subsystem error codes. Values are stable: they may be serialized
// across sessions, so existing codes never change meaning and new codes are
// appended. Each code maps to exactly one core::ErrorCategory.
enum class PlatformError : uint8_t {
    INVALID_SOURCE = 0,  ///< invalid_argument - the input source is not a known one
    INVALID_CODE,        ///< invalid_argument - the code is outside its source's range
    ACTION_OUT_OF_RANGE, ///< invalid_argument - the action id exceeds the action table
    WINDOW_CLOSED,       ///< invalid_state - the window is closed
    ALREADY_INITIALIZED, ///< invalid_state - init() called a second time
    UNSUPPORTED,         ///< not_supported - a capability the platform does not provide
    INVALID_SIZE,        ///< invalid_argument - a window dimension is zero
    ALLOCATION_FAILED,   ///< resource - the allocator could not satisfy a window request
};

// Maps a PlatformError to its category. Constexpr so mappings are verifiable
// at compile time. Total: out-of-range codes (corrupted values) map to
// invalid_state instead of undefined behavior.
[[nodiscard]] constexpr core::ErrorCategory categoryOf(PlatformError code) noexcept {
    switch (code) {
    case PlatformError::INVALID_SOURCE:
    case PlatformError::INVALID_CODE:
    case PlatformError::ACTION_OUT_OF_RANGE:
    case PlatformError::INVALID_SIZE:
        return core::ErrorCategory::INVALID_ARGUMENT;
    case PlatformError::WINDOW_CLOSED:
    case PlatformError::ALREADY_INITIALIZED:
        return core::ErrorCategory::INVALID_STATE;
    case PlatformError::UNSUPPORTED:
        return core::ErrorCategory::NOT_SUPPORTED;
    case PlatformError::ALLOCATION_FAILED:
        return core::ErrorCategory::RESOURCE;
    }
    return core::ErrorCategory::INVALID_STATE;
}

// Human-readable name of a PlatformError ("invalid_code"). Stable and meant
// for logging and diagnostics, never parsed (rule 04). Out-of-range codes
// return "unknown".
[[nodiscard]] constexpr std::string_view toString(PlatformError code) noexcept {
    switch (code) {
    case PlatformError::INVALID_SOURCE:
        return "invalid_source";
    case PlatformError::INVALID_CODE:
        return "invalid_code";
    case PlatformError::ACTION_OUT_OF_RANGE:
        return "action_out_of_range";
    case PlatformError::WINDOW_CLOSED:
        return "window_closed";
    case PlatformError::ALREADY_INITIALIZED:
        return "already_initialized";
    case PlatformError::UNSUPPORTED:
        return "unsupported";
    case PlatformError::INVALID_SIZE:
        return "invalid_size";
    case PlatformError::ALLOCATION_FAILED:
        return "allocation_failed";
    }
    return "unknown";
}

// Expected alias for platform errors.
template <typename T> using Expected = std::expected<T, PlatformError>;

// Void convenience alias for operations that only signal success or failure.
using ExpectedVoid = std::expected<void, PlatformError>;

} // namespace infinity::platform

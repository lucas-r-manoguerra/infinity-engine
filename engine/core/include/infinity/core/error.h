// infinity/core/error.h
//
// Error vocabulary (F2.3, ADR-003, rule 04). The engine has no exceptions:
// recoverable failures are reported through std::expected<T, E>, programming
// errors are assert/panic, and allocation failures never reach the hot path
// (rule 03: buffers are pre-reserved). This header is the vocabulary ONLY:
// a per-subsystem code enum mapped to a small set of categories, plus the
// Expected aliases the engine returns through.
//
//   Model      - Recoverable -> std::expected<T, CoreError>. The caller
//                handles the error, translates it at module boundaries, or
//                closes it at the system boundary with logging; swallowing
//                an error is a bug (rule 04).
//   Categories - The five categories mandated by rule 04 (init, io, resource,
//                invalid_state, not_supported) plus invalid_argument, an
//                extension for core validation that is consistent with
//                ADR-003: a bad caller-supplied argument is a recoverable
//                call error, not a programming invariant.
//   Subsystems - core returns CoreError. Non-core modules (renderer, ecs,
//                ai, ...) define their own error enums when they ship; they
//                reuse ErrorCategory so category mapping and logging stay
//                uniform across the engine.
//   Totality   - categoryOf/toString cover every enumerator and define a
//                fallback for out-of-range values, so a corrupted code can
//                never crash nor yield undefined behavior.
#pragma once

#include <cstdint>
#include <expected>
#include <string_view>

namespace infinity::core {

// Error categories (rule 04): every subsystem error code maps to exactly one
// category, so a call site can decide how to react without knowing the
// subsystem that produced the error.
enum class ErrorCategory : uint8_t {
    INIT = 0,         ///< setup or initialization failed
    IO,               ///< reading or writing external data failed
    RESOURCE,         ///< memory, handle, or resource exhausted or absent
    INVALID_STATE,    ///< operation requires a state the object is not in
    NOT_SUPPORTED,    ///< capability the engine does not provide
    INVALID_ARGUMENT, ///< a caller-supplied argument is not usable
};

// Core subsystem error codes (rule 04: E is a per-subsystem enum). Each code
// maps to exactly one ErrorCategory via categoryOf(). Values are stable:
// they may be serialized across sessions, so existing codes never change
// meaning and new codes are appended.
enum class CoreError : uint8_t {
    ALLOCATION_FAILED = 0,    ///< resource - the allocator could not satisfy a request
    BACKING_ALLOCATOR_FAILED, ///< resource - the backing allocator of an arena/pool failed
    NOT_FOUND,                ///< resource - a requested item does not exist
    TIMEOUT,                  ///< resource - an operation exceeded its time budget
    ALREADY_INITIALIZED,      ///< invalid_state - init() called a second time
    NOT_INITIALIZED,          ///< invalid_state - operation issued before init()
    INVALID_ALIGNMENT,        ///< invalid_argument - alignment is not a power of two >= 1
    INVALID_SIZE,             ///< invalid_argument - size or count is outside its valid range
    OUT_OF_BOUNDS,            ///< invalid_argument - index exceeds a container's bounds
    UNSUPPORTED,              ///< not_supported - capability is not implemented
    IO_ERROR,                 ///< io - a read/write failed for an unspecified reason
    IO_NOT_FOUND,             ///< io - the file or path does not exist
    IO_PERMISSION_DENIED,     ///< io - the file or path is not accessible
    IO_INVALID_DATA,          ///< io - the data does not match the expected format
    INVALID_UTF8, ///< invalid_argument - a byte sequence is not well-formed UTF-8 (F2.8a, Unicode
                  ///< Table 3-7)
    IO_ALREADY_EXISTS,  ///< io - the file or path already exists (F2.8, ADR-023)
    IO_WRONG_TYPE,      ///< io - the path exists but has the wrong kind (file vs directory) (F2.8,
                        ///< ADR-023)
    DUPLICATE_SYSTEM,   ///< invalid_argument - a system name is already registered (F2.11, ADR-014)
    UNKNOWN_DEPENDENCY, ///< invalid_argument - a declared dependency is not a registered system
                        ///< (F2.11, ADR-014)
    DEPENDENCY_CYCLE,   ///< invalid_state - system dependencies form a cycle (F2.11, ADR-014)
    INVALID_ARGUMENT,   ///< invalid_argument - a caller-supplied argument is not usable (F2.11,
                        ///< ADR-014)
    DUPLICATE_BUDGET,   ///< invalid_argument - a budget id is already registered (F2.12,
                        ///< ADR-034)
};

// Maps a CoreError to its category. Constexpr so mappings are verifiable at
// compile time. Total: out-of-range codes (corrupted values) map to
// invalid_state instead of undefined behavior.
[[nodiscard]] constexpr ErrorCategory categoryOf(CoreError code) noexcept {
    switch (code) {
    case CoreError::ALLOCATION_FAILED:
    case CoreError::BACKING_ALLOCATOR_FAILED:
    case CoreError::NOT_FOUND:
    case CoreError::TIMEOUT:
        return ErrorCategory::RESOURCE;
    case CoreError::ALREADY_INITIALIZED:
    case CoreError::NOT_INITIALIZED:
    case CoreError::DEPENDENCY_CYCLE:
        return ErrorCategory::INVALID_STATE;
    case CoreError::INVALID_ALIGNMENT:
    case CoreError::INVALID_SIZE:
    case CoreError::OUT_OF_BOUNDS:
    case CoreError::INVALID_UTF8:
    case CoreError::DUPLICATE_SYSTEM:
    case CoreError::UNKNOWN_DEPENDENCY:
    case CoreError::INVALID_ARGUMENT:
    case CoreError::DUPLICATE_BUDGET:
        return ErrorCategory::INVALID_ARGUMENT;
    case CoreError::UNSUPPORTED:
        return ErrorCategory::NOT_SUPPORTED;
    case CoreError::IO_ERROR:
    case CoreError::IO_NOT_FOUND:
    case CoreError::IO_PERMISSION_DENIED:
    case CoreError::IO_INVALID_DATA:
    case CoreError::IO_ALREADY_EXISTS:
    case CoreError::IO_WRONG_TYPE:
        return ErrorCategory::IO;
    }
    return ErrorCategory::INVALID_STATE;
}

// Human-readable name of a CoreError ("allocation_failed"). Stable and meant
// for logging and diagnostics, never parsed (rule 04). Out-of-range codes
// return "unknown".
[[nodiscard]] constexpr std::string_view toString(CoreError code) noexcept {
    switch (code) {
    case CoreError::ALLOCATION_FAILED:
        return "allocation_failed";
    case CoreError::BACKING_ALLOCATOR_FAILED:
        return "backing_allocator_failed";
    case CoreError::NOT_FOUND:
        return "not_found";
    case CoreError::TIMEOUT:
        return "timeout";
    case CoreError::ALREADY_INITIALIZED:
        return "already_initialized";
    case CoreError::NOT_INITIALIZED:
        return "not_initialized";
    case CoreError::INVALID_ALIGNMENT:
        return "invalid_alignment";
    case CoreError::INVALID_SIZE:
        return "invalid_size";
    case CoreError::OUT_OF_BOUNDS:
        return "out_of_bounds";
    case CoreError::UNSUPPORTED:
        return "unsupported";
    case CoreError::IO_ERROR:
        return "io_error";
    case CoreError::IO_NOT_FOUND:
        return "io_not_found";
    case CoreError::IO_PERMISSION_DENIED:
        return "io_permission_denied";
    case CoreError::IO_INVALID_DATA:
        return "io_invalid_data";
    case CoreError::IO_ALREADY_EXISTS:
        return "io_already_exists";
    case CoreError::IO_WRONG_TYPE:
        return "io_wrong_type";
    case CoreError::INVALID_UTF8:
        return "invalid_utf8";
    case CoreError::DUPLICATE_SYSTEM:
        return "duplicate_system";
    case CoreError::UNKNOWN_DEPENDENCY:
        return "unknown_dependency";
    case CoreError::DEPENDENCY_CYCLE:
        return "dependency_cycle";
    case CoreError::INVALID_ARGUMENT:
        return "invalid_argument";
    case CoreError::DUPLICATE_BUDGET:
        return "duplicate_budget";
    }
    return "unknown";
}

// Human-readable name of an ErrorCategory ("resource"). Stable, for logging.
// Out-of-range values return "unknown".
[[nodiscard]] constexpr std::string_view toString(ErrorCategory category) noexcept {
    switch (category) {
    case ErrorCategory::INIT:
        return "init";
    case ErrorCategory::IO:
        return "io";
    case ErrorCategory::RESOURCE:
        return "resource";
    case ErrorCategory::INVALID_STATE:
        return "invalid_state";
    case ErrorCategory::NOT_SUPPORTED:
        return "not_supported";
    case ErrorCategory::INVALID_ARGUMENT:
        return "invalid_argument";
    }
    return "unknown";
}

// Expected alias for core errors: the engine's core subsystems return these.
// See the header brief for the error model (ADR-003).
template <typename T> using Expected = std::expected<T, CoreError>;

// Void convenience alias for operations that only signal success or failure.
using ExpectedVoid = std::expected<void, CoreError>;

} // namespace infinity::core

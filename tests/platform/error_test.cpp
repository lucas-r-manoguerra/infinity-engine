// tests/platform/error_test.cpp
//
// Contract tests for the platform error vocabulary (F3, ADR-003, rule 04):
// every PlatformError maps to its documented core::ErrorCategory, the string
// names are stable and distinct, categoryOf is constexpr, and out-of-range
// codes hit a documented fallback. The platform reuses core's ErrorCategory
// so logging and category handling stay uniform across the engine.
//
// ADL note: these enums are never compared inside CHECK; comparisons go
// through the isMappedTo helper or string_views (tests/core/error_test.cpp).
#include "infinity/platform/error.h"

#include "infinity/core/error.h"

#include <array>
#include <cstddef>
#include <ostream>
#include <string_view>

#include <doctest/doctest.h>

// categoryOf is a pure constexpr mapping (rule 02/04): the same category
// must be derivable at compile time and at runtime.
static_assert(infinity::platform::categoryOf(infinity::platform::PlatformError::INVALID_SOURCE) ==
              infinity::core::ErrorCategory::INVALID_ARGUMENT);
static_assert(infinity::platform::categoryOf(infinity::platform::PlatformError::WINDOW_CLOSED) ==
              infinity::core::ErrorCategory::INVALID_STATE);
static_assert(infinity::platform::categoryOf(infinity::platform::PlatformError::UNSUPPORTED) ==
              infinity::core::ErrorCategory::NOT_SUPPORTED);
static_assert(
    infinity::platform::categoryOf(infinity::platform::PlatformError::ALLOCATION_FAILED) ==
    infinity::core::ErrorCategory::RESOURCE);

namespace {

// Table-driven contract data: every PlatformError with the category and the
// stable name it is documented to map to. These three columns are the whole
// public contract of the vocabulary, so one table feeds every table test.
struct CodeCategory {
    infinity::platform::PlatformError code;
    infinity::core::ErrorCategory category;
    std::string_view name;
};

constexpr std::array<CodeCategory, 8> ALL_CODES{
    CodeCategory{.code = infinity::platform::PlatformError::INVALID_SOURCE,
                 .category = infinity::core::ErrorCategory::INVALID_ARGUMENT,
                 .name = "invalid_source"},
    CodeCategory{.code = infinity::platform::PlatformError::INVALID_CODE,
                 .category = infinity::core::ErrorCategory::INVALID_ARGUMENT,
                 .name = "invalid_code"},
    CodeCategory{.code = infinity::platform::PlatformError::ACTION_OUT_OF_RANGE,
                 .category = infinity::core::ErrorCategory::INVALID_ARGUMENT,
                 .name = "action_out_of_range"},
    CodeCategory{.code = infinity::platform::PlatformError::WINDOW_CLOSED,
                 .category = infinity::core::ErrorCategory::INVALID_STATE,
                 .name = "window_closed"},
    CodeCategory{.code = infinity::platform::PlatformError::ALREADY_INITIALIZED,
                 .category = infinity::core::ErrorCategory::INVALID_STATE,
                 .name = "already_initialized"},
    CodeCategory{.code = infinity::platform::PlatformError::UNSUPPORTED,
                 .category = infinity::core::ErrorCategory::NOT_SUPPORTED,
                 .name = "unsupported"},
    CodeCategory{.code = infinity::platform::PlatformError::INVALID_SIZE,
                 .category = infinity::core::ErrorCategory::INVALID_ARGUMENT,
                 .name = "invalid_size"},
    CodeCategory{.code = infinity::platform::PlatformError::ALLOCATION_FAILED,
                 .category = infinity::core::ErrorCategory::RESOURCE,
                 .name = "allocation_failed"},
};

// Category names of core::ErrorCategory (stable, for logging).
constexpr std::array<std::string_view, 6> CATEGORY_NAMES{
    "init", "io", "resource", "invalid_state", "not_supported", "invalid_argument"};

// Returns true when categoryOf(code) is category. Isolated from CHECK so
// doctest never has to stringify a PlatformError operand (ADL).
[[nodiscard]] bool isMappedTo(infinity::platform::PlatformError code,
                              infinity::core::ErrorCategory category) noexcept {
    return infinity::platform::categoryOf(code) == category;
}

} // namespace

TEST_CASE("every PlatformError maps to its documented category") {
    for (const CodeCategory& entry : ALL_CODES) {
        CAPTURE(entry.name);
        CHECK(isMappedTo(entry.code, entry.category));
    }
}

TEST_CASE("toString returns the documented stable name for every code") {
    for (const CodeCategory& entry : ALL_CODES) {
        const std::string_view name = infinity::platform::toString(entry.code);
        CAPTURE(entry.name);
        CHECK(name == entry.name);
        CHECK_FALSE(name.empty());
    }
}

TEST_CASE("toString returns distinct strings for distinct codes") {
    for (size_t i = 0; i < ALL_CODES.size(); ++i) {
        const std::string_view name = infinity::platform::toString(ALL_CODES[i].code);
        for (size_t j = i + 1; j < ALL_CODES.size(); ++j) {
            CHECK(name != infinity::platform::toString(ALL_CODES[j].code));
        }
    }
}

TEST_CASE("toString returns the documented name for every category") {
    for (size_t i = 0; i < CATEGORY_NAMES.size(); ++i) {
        const auto category = static_cast<infinity::core::ErrorCategory>(i);
        CHECK(infinity::core::toString(category) == CATEGORY_NAMES[i]);
    }
}

TEST_CASE("Expected<T, PlatformError> carries a value and an error") {
    const infinity::platform::Expected<int> value{42};
    CHECK(value.has_value());
    CHECK(*value == 42);

    const infinity::platform::Expected<int> failure{
        std::unexpect, infinity::platform::PlatformError::WINDOW_CLOSED};
    const std::string_view windowClosed = "window_closed";
    CHECK_FALSE(failure.has_value());
    CHECK(infinity::platform::toString(failure.error()) == windowClosed);
    CHECK(isMappedTo(failure.error(), infinity::core::ErrorCategory::INVALID_STATE));
}

TEST_CASE("ExpectedVoid reports success and error") {
    const infinity::platform::ExpectedVoid success{};
    CHECK(success.has_value());

    const infinity::platform::ExpectedVoid failure{std::unexpect,
                                                   infinity::platform::PlatformError::UNSUPPORTED};
    CHECK_FALSE(failure.has_value());
    CHECK(isMappedTo(failure.error(), infinity::core::ErrorCategory::NOT_SUPPORTED));
}

TEST_CASE("toString of an out-of-range code returns the documented fallback") {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- deliberate out-of-range code.
    const auto invalid = static_cast<infinity::platform::PlatformError>(0xEE);
    const std::string_view unknown = "unknown";
    const std::string_view invalidState = "invalid_state";
    CHECK(infinity::platform::toString(invalid) == unknown);
    CHECK_FALSE(infinity::platform::toString(invalid).empty());
    CHECK(infinity::core::toString(infinity::platform::categoryOf(invalid)) == invalidState);
}

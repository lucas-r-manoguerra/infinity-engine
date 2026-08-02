// tests/renderer/error_test.cpp
//
// Contract tests for the renderer error vocabulary (F4, ADR-003, rule 04):
// every RenderError maps to its documented core::ErrorCategory, the string
// names are stable and distinct, categoryOf is constexpr, and out-of-range
// codes hit a documented fallback. The renderer reuses core's ErrorCategory
// so logging and category handling stay uniform across the engine.
//
// ADL note: these enums are never compared inside CHECK; comparisons go
// through the isMappedTo helper or string_views (tests/core/error_test.cpp).
#include "infinity/renderer/error.h"

#include "infinity/core/error.h"

#include <array>
#include <cstddef>
#include <ostream>
#include <string_view>

#include <doctest/doctest.h>

// categoryOf is a pure constexpr mapping (rule 02/04): the same category
// must be derivable at compile time and at runtime.
static_assert(infinity::renderer::categoryOf(infinity::renderer::RenderError::INVALID_ARGUMENT) ==
              infinity::core::ErrorCategory::INVALID_ARGUMENT);
static_assert(infinity::renderer::categoryOf(infinity::renderer::RenderError::INVALID_SIZE) ==
              infinity::core::ErrorCategory::INVALID_ARGUMENT);
static_assert(infinity::renderer::categoryOf(infinity::renderer::RenderError::INVALID_STATE) ==
              infinity::core::ErrorCategory::INVALID_STATE);
static_assert(infinity::renderer::categoryOf(infinity::renderer::RenderError::NOT_SUPPORTED) ==
              infinity::core::ErrorCategory::NOT_SUPPORTED);
static_assert(infinity::renderer::categoryOf(infinity::renderer::RenderError::ALLOCATION_FAILED) ==
              infinity::core::ErrorCategory::RESOURCE);
static_assert(infinity::renderer::categoryOf(infinity::renderer::RenderError::INTERNAL) ==
              infinity::core::ErrorCategory::INVALID_STATE);

namespace {

// Table-driven contract data: every RenderError with the category and the
// stable name it is documented to map to. These three columns are the whole
// public contract of the vocabulary, so one table feeds every table test.
struct CodeCategory {
    infinity::renderer::RenderError code;
    infinity::core::ErrorCategory category;
    std::string_view name;
};

constexpr std::array<CodeCategory, 6> ALL_CODES{
    CodeCategory{.code = infinity::renderer::RenderError::INVALID_ARGUMENT,
                 .category = infinity::core::ErrorCategory::INVALID_ARGUMENT,
                 .name = "invalid_argument"},
    CodeCategory{.code = infinity::renderer::RenderError::INVALID_SIZE,
                 .category = infinity::core::ErrorCategory::INVALID_ARGUMENT,
                 .name = "invalid_size"},
    CodeCategory{.code = infinity::renderer::RenderError::INVALID_STATE,
                 .category = infinity::core::ErrorCategory::INVALID_STATE,
                 .name = "invalid_state"},
    CodeCategory{.code = infinity::renderer::RenderError::NOT_SUPPORTED,
                 .category = infinity::core::ErrorCategory::NOT_SUPPORTED,
                 .name = "not_supported"},
    CodeCategory{.code = infinity::renderer::RenderError::ALLOCATION_FAILED,
                 .category = infinity::core::ErrorCategory::RESOURCE,
                 .name = "allocation_failed"},
    CodeCategory{.code = infinity::renderer::RenderError::INTERNAL,
                 .category = infinity::core::ErrorCategory::INVALID_STATE,
                 .name = "internal"},
};

// Returns true when categoryOf(code) is category. Isolated from CHECK so
// doctest never has to stringify a RenderError operand (ADL).
[[nodiscard]] bool isMappedTo(infinity::renderer::RenderError code,
                              infinity::core::ErrorCategory category) noexcept {
    return infinity::renderer::categoryOf(code) == category;
}

} // namespace

TEST_CASE("every RenderError maps to its documented category") {
    for (const CodeCategory& entry : ALL_CODES) {
        CAPTURE(entry.name);
        CHECK(isMappedTo(entry.code, entry.category));
    }
}

TEST_CASE("toString returns the documented stable name for every code") {
    for (const CodeCategory& entry : ALL_CODES) {
        const std::string_view name = infinity::renderer::toString(entry.code);
        CAPTURE(entry.name);
        CHECK(name == entry.name);
        CHECK_FALSE(name.empty());
    }
}

TEST_CASE("toString returns distinct strings for distinct codes") {
    for (size_t i = 0; i < ALL_CODES.size(); ++i) {
        const std::string_view name = infinity::renderer::toString(ALL_CODES[i].code);
        for (size_t j = i + 1; j < ALL_CODES.size(); ++j) {
            CHECK(name != infinity::renderer::toString(ALL_CODES[j].code));
        }
    }
}

TEST_CASE("Expected<T, RenderError> carries a value and an error") {
    const infinity::renderer::Expected<int> value{42};
    CHECK(value.has_value());
    CHECK(*value == 42);

    const infinity::renderer::Expected<int> failure{
        std::unexpect, infinity::renderer::RenderError::INVALID_ARGUMENT};
    const std::string_view invalidArgument = "invalid_argument";
    CHECK_FALSE(failure.has_value());
    CHECK(infinity::renderer::toString(failure.error()) == invalidArgument);
    CHECK(isMappedTo(failure.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
}

TEST_CASE("ExpectedVoid reports success and error") {
    const infinity::renderer::ExpectedVoid success{};
    CHECK(success.has_value());

    const infinity::renderer::ExpectedVoid failure{std::unexpect,
                                                   infinity::renderer::RenderError::NOT_SUPPORTED};
    CHECK_FALSE(failure.has_value());
    CHECK(isMappedTo(failure.error(), infinity::core::ErrorCategory::NOT_SUPPORTED));
}

TEST_CASE("toString of an out-of-range code returns the documented fallback") {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- deliberate out-of-range code.
    const auto invalid = static_cast<infinity::renderer::RenderError>(0xEE);
    const std::string_view unknown = "unknown";
    const std::string_view invalidState = "invalid_state";
    CHECK(infinity::renderer::toString(invalid) == unknown);
    CHECK_FALSE(infinity::renderer::toString(invalid).empty());
    CHECK(infinity::core::toString(infinity::renderer::categoryOf(invalid)) == invalidState);
}

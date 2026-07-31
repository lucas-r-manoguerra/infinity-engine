// tests/core/error_test.cpp
//
// Contract tests for the error vocabulary (F2.3, ADR-003, rule 04): every
// CoreError maps to its documented category, the string names are stable and
// distinct, the Expected aliases carry values and errors, categoryOf is
// constexpr, and out-of-range codes hit a documented fallback. Never calls
// std::expected::value(): under -fno-exceptions it would terminate on an
// error expected, so the tests go through has_value(), operator* and error().
//
// ADL note: infinity::core::toString would win argument-dependent lookup over
// doctest's own toString when doctest decompiles an expression that mentions
// a CoreError/ErrorCategory operand, so assertions never compare those enums
// inside CHECK - they compare string_views or bools instead.
#include "infinity/core/error.h"

#include <array>
#include <cstddef>
#include <expected>
#include <ostream>
#include <string_view>

#include <doctest/doctest.h>

// categoryOf is a pure constexpr mapping (rule 02/04): the same category
// must be derivable at compile time and at runtime.
static_assert(infinity::core::categoryOf(infinity::core::CoreError::ALLOCATION_FAILED) ==
              infinity::core::ErrorCategory::RESOURCE);
static_assert(infinity::core::categoryOf(infinity::core::CoreError::NOT_INITIALIZED) ==
              infinity::core::ErrorCategory::INVALID_STATE);
static_assert(infinity::core::categoryOf(infinity::core::CoreError::IO_PERMISSION_DENIED) ==
              infinity::core::ErrorCategory::IO);

namespace {

// Table-driven contract data: every CoreError with the category and the
// stable name it is documented to map to. These three columns are the whole
// public contract of the vocabulary, so one table feeds every table test.
struct CodeCategory {
    infinity::core::CoreError code;
    infinity::core::ErrorCategory category;
    std::string_view name;
};

constexpr std::array<CodeCategory, 14> ALL_CODES{
    CodeCategory{.code = infinity::core::CoreError::ALLOCATION_FAILED,
                 .category = infinity::core::ErrorCategory::RESOURCE,
                 .name = "allocation_failed"},
    CodeCategory{.code = infinity::core::CoreError::BACKING_ALLOCATOR_FAILED,
                 .category = infinity::core::ErrorCategory::RESOURCE,
                 .name = "backing_allocator_failed"},
    CodeCategory{.code = infinity::core::CoreError::NOT_FOUND,
                 .category = infinity::core::ErrorCategory::RESOURCE,
                 .name = "not_found"},
    CodeCategory{.code = infinity::core::CoreError::TIMEOUT,
                 .category = infinity::core::ErrorCategory::RESOURCE,
                 .name = "timeout"},
    CodeCategory{.code = infinity::core::CoreError::ALREADY_INITIALIZED,
                 .category = infinity::core::ErrorCategory::INVALID_STATE,
                 .name = "already_initialized"},
    CodeCategory{.code = infinity::core::CoreError::NOT_INITIALIZED,
                 .category = infinity::core::ErrorCategory::INVALID_STATE,
                 .name = "not_initialized"},
    CodeCategory{.code = infinity::core::CoreError::INVALID_ALIGNMENT,
                 .category = infinity::core::ErrorCategory::INVALID_ARGUMENT,
                 .name = "invalid_alignment"},
    CodeCategory{.code = infinity::core::CoreError::INVALID_SIZE,
                 .category = infinity::core::ErrorCategory::INVALID_ARGUMENT,
                 .name = "invalid_size"},
    CodeCategory{.code = infinity::core::CoreError::OUT_OF_BOUNDS,
                 .category = infinity::core::ErrorCategory::INVALID_ARGUMENT,
                 .name = "out_of_bounds"},
    CodeCategory{.code = infinity::core::CoreError::UNSUPPORTED,
                 .category = infinity::core::ErrorCategory::NOT_SUPPORTED,
                 .name = "unsupported"},
    CodeCategory{.code = infinity::core::CoreError::IO_ERROR,
                 .category = infinity::core::ErrorCategory::IO,
                 .name = "io_error"},
    CodeCategory{.code = infinity::core::CoreError::IO_NOT_FOUND,
                 .category = infinity::core::ErrorCategory::IO,
                 .name = "io_not_found"},
    CodeCategory{.code = infinity::core::CoreError::IO_PERMISSION_DENIED,
                 .category = infinity::core::ErrorCategory::IO,
                 .name = "io_permission_denied"},
    CodeCategory{.code = infinity::core::CoreError::IO_INVALID_DATA,
                 .category = infinity::core::ErrorCategory::IO,
                 .name = "io_invalid_data"},
};

// Category names of ErrorCategory (stable, for logging).
constexpr std::array<std::string_view, 6> CATEGORY_NAMES{
    "init", "io", "resource", "invalid_state", "not_supported", "invalid_argument"};

// Returns true when categoryOf(code) is category. Isolated from CHECK so
// doctest never has to stringify a CoreError/ErrorCategory operand (ADL).
[[nodiscard]] bool isMappedTo(infinity::core::CoreError code,
                              infinity::core::ErrorCategory category) noexcept {
    return infinity::core::categoryOf(code) == category;
}

} // namespace

TEST_CASE("every CoreError maps to its documented category") {
    for (const CodeCategory& entry : ALL_CODES) {
        CAPTURE(entry.name);
        CHECK(isMappedTo(entry.code, entry.category));
    }
}

TEST_CASE("toString returns the documented stable name for every code") {
    for (const CodeCategory& entry : ALL_CODES) {
        const std::string_view name = infinity::core::toString(entry.code);
        CAPTURE(entry.name);
        CHECK(name == entry.name);
        CHECK_FALSE(name.empty());
    }
}

TEST_CASE("toString returns distinct strings for distinct codes") {
    for (size_t i = 0; i < ALL_CODES.size(); ++i) {
        const std::string_view name = infinity::core::toString(ALL_CODES[i].code);
        for (size_t j = i + 1; j < ALL_CODES.size(); ++j) {
            CHECK(name != infinity::core::toString(ALL_CODES[j].code));
        }
    }
}

TEST_CASE("toString returns the documented name for every category") {
    for (size_t i = 0; i < CATEGORY_NAMES.size(); ++i) {
        const auto category = static_cast<infinity::core::ErrorCategory>(i);
        CHECK(infinity::core::toString(category) == CATEGORY_NAMES[i]);
    }
}

TEST_CASE("Expected<int> carries a value") {
    const infinity::core::Expected<int> value{42};
    CHECK(value.has_value());
    CHECK(*value == 42);
}

TEST_CASE("ExpectedVoid reports success") {
    const infinity::core::ExpectedVoid success{};
    CHECK(success.has_value());
}

TEST_CASE("an error result reports its code and category") {
    const infinity::core::Expected<int> failure{std::unexpect,
                                                infinity::core::CoreError::ALLOCATION_FAILED};
    const std::string_view allocationFailed = "allocation_failed";
    CHECK_FALSE(failure.has_value());
    CHECK(infinity::core::toString(failure.error()) == allocationFailed);
    CHECK(isMappedTo(failure.error(), infinity::core::ErrorCategory::RESOURCE));
}

TEST_CASE("Expected works with aggregate value types") {
    const infinity::core::Expected<std::array<float, 4>> quad{
        std::array<float, 4>{1.0f, 2.0f, 3.0f, 4.0f}};
    CHECK(quad.has_value());
    CHECK((*quad)[0] == 1.0f);
    CHECK((*quad)[3] == 4.0f);
}

TEST_CASE("toString of an out-of-range code returns the documented fallback") {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- deliberate out-of-range code.
    const auto invalid = static_cast<infinity::core::CoreError>(0xEE);
    const std::string_view unknown = "unknown";
    const std::string_view invalidState = "invalid_state";
    CHECK(infinity::core::toString(invalid) == unknown);
    CHECK_FALSE(infinity::core::toString(invalid).empty());
    CHECK(infinity::core::toString(infinity::core::categoryOf(invalid)) == invalidState);
}

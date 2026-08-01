// tests/core/utf8_view_test.cpp
//
// Contract tests for Utf8View and isValidUtf8 (F2.8a, ADR-023, rule 11):
// Utf8View yields every code point of a string in order and reports each
// invalid byte once as INVALID_UTF8, advancing one byte past it (documented
// resync policy) - errors are surfaced, never substituted (rule 04);
// isValidUtf8 accepts exactly the well-formed sequences. The shared case
// tables live in utf8_test_data.h; the decode/encode contract lives in
// utf8_test.cpp.
#include "infinity/core/utf8.h"
#include "utf8_test_data.h"

#include <array>
#include <cstddef>
#include <ostream>
#include <string_view>

#include <doctest/doctest.h>

TEST_CASE("Utf8View yields every code point of a mixed string in order") {
    // "A" + U+00E9 + U+20AC + U+1F600: one code point per byte length 1..4.
    const infinity::core::Utf8View view("A"
                                        "\xC3\xA9"
                                        "\xE2\x82\xAC"
                                        "\xF0\x9F\x98\x80");
    constexpr std::array<char32_t, 4> EXPECTED_CODE_POINTS{0x41, 0x00E9, 0x20AC, 0x1F600};
    std::size_t count = 0;
    for (const infinity::core::Expected<infinity::core::Utf8Decoded> decoded : view) {
        if (decoded.has_value() && count < EXPECTED_CODE_POINTS.size()) {
            CHECK(decoded->codePoint == EXPECTED_CODE_POINTS[count]);
            ++count;
        } else {
            CHECK(decoded.has_value());
        }
    }
    CHECK(count == EXPECTED_CODE_POINTS.size());
}

TEST_CASE("Utf8View of an empty string yields no code points") {
    const infinity::core::Utf8View view("");
    std::size_t count = 0;
    for (auto it = view.begin(); it != view.end(); ++it) {
        ++count;
    }
    CHECK(count == 0);
}

TEST_CASE("Utf8View reports invalid input as errors instead of substituting") {
    // Scenario 1: "A" + overlong NUL (C0 80) + "B". Each invalid byte is
    // reported once and the iterator advances one byte past it (the
    // documented resync policy), then reaches 'B'.
    const infinity::core::Utf8View overlong("A"
                                            "\xC0\x80"
                                            "B");
    constexpr std::array<char32_t, 2> EXPECTED_CODE_POINTS{0x41, 0x42};
    std::size_t codePoints = 0;
    std::size_t errors = 0;
    for (const infinity::core::Expected<infinity::core::Utf8Decoded> decoded : overlong) {
        if (decoded.has_value()) {
            if (codePoints < EXPECTED_CODE_POINTS.size()) {
                CHECK(decoded->codePoint == EXPECTED_CODE_POINTS[codePoints]);
            }
            ++codePoints;
        } else {
            CHECK(failsWith(infinity::core::CoreError::INVALID_UTF8, decoded));
            ++errors;
        }
    }
    CHECK(codePoints == EXPECTED_CODE_POINTS.size());
    CHECK(errors == 2);

    // Scenario 2: "A" + stray continuation byte + "B": one error, then 'B'.
    const infinity::core::Utf8View stray("A"
                                         "\x80"
                                         "B");
    std::size_t strayCodePoints = 0;
    std::size_t strayErrors = 0;
    for (const infinity::core::Expected<infinity::core::Utf8Decoded> decoded : stray) {
        if (decoded.has_value()) {
            ++strayCodePoints;
        } else {
            CHECK(failsWith(infinity::core::CoreError::INVALID_UTF8, decoded));
            ++strayErrors;
        }
    }
    CHECK(strayCodePoints == 2);
    CHECK(strayErrors == 1);
}

TEST_CASE("isValidUtf8 accepts every well-formed sequence") {
    CHECK(infinity::core::isValidUtf8(""));
    for (const ValidCase& entry : VALID_CASES) {
        CHECK(infinity::core::isValidUtf8(entry.bytes));
    }
    // A concatenation of sequences of every length stays well-formed.
    CHECK(infinity::core::isValidUtf8("A"
                                      "\xC3\xA9"
                                      "\xE2\x82\xAC"
                                      "\xF0\x9F\x98\x80"));
}

TEST_CASE("isValidUtf8 rejects every malformed sequence") {
    for (const std::string_view bytes : INVALID_CASES) {
        CHECK_FALSE(infinity::core::isValidUtf8(bytes));
    }
    // The whole sequence is malformed even when the FIRST code point is not:
    // a stray continuation between ASCII letters fails validation.
    CHECK_FALSE(infinity::core::isValidUtf8("\x41\x80\x42"));
}

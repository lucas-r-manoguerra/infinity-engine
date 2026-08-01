// tests/core/utf8_test_data.h
//
// Shared fixtures for the UTF-8 contract tests (F2.8a, ADR-023, rule 11):
// every well-formed and malformed case of Unicode Table 3-7, plus the
// error-checking helper. Lives in tests/, never in the public include tree;
// both utf8_test.cpp and utf8_view_test.cpp consume it so the case tables
// cannot drift between the decode/encode contract and the iteration contract.
#pragma once

#include "infinity/core/error.h"

#include <array>
#include <cstdint>
#include <string_view>

// One well-formed case: the bytes, the code point they decode to, and the
// byte length. Covers every Table 3-7 row and its boundaries.
struct ValidCase {
    std::string_view bytes;
    char32_t codePoint;
    uint8_t byteLength;
};

// NUL is a real 1-byte sequence: the explicit-length view is required because
// a string_view built from a const char* counts via strlen and would truncate
// at the NUL byte.
constexpr std::array<ValidCase, 20> VALID_CASES{
    ValidCase{.bytes = std::string_view("\x00", 1),
              .codePoint = 0x000000,
              .byteLength = 1},                                         // U+0000 - shortest ASCII
    ValidCase{.bytes = "A", .codePoint = 0x000041, .byteLength = 1},    // ASCII letter
    ValidCase{.bytes = "\x7F", .codePoint = 0x00007F, .byteLength = 1}, // last 1-byte code point
    ValidCase{
        .bytes = "\xC2\x80", .codePoint = 0x000080, .byteLength = 2}, // first 2-byte code point
    ValidCase{
        .bytes = "\xDF\xBF", .codePoint = 0x0007FF, .byteLength = 2}, // last 2-byte code point
    ValidCase{
        .bytes = "\xC3\xA9", .codePoint = 0x0000E9, .byteLength = 2}, // U+00E9, real-world 2-byte
    ValidCase{.bytes = "\xE0\xA0\x80",
              .codePoint = 0x000800,
              .byteLength = 3}, // first 3-byte (overlong boundary)
    ValidCase{.bytes = "\xE0\xBF\xBF", .codePoint = 0x000FFF, .byteLength = 3}, // end of the E0 row
    ValidCase{
        .bytes = "\xE1\x80\x80", .codePoint = 0x001000, .byteLength = 3}, // start of the E1 row
    ValidCase{.bytes = "\xE2\x82\xAC", .codePoint = 0x0020AC, .byteLength = 3}, // U+20AC euro sign
    ValidCase{.bytes = "\xED\x9F\xBF",
              .codePoint = 0x00D7FF,
              .byteLength = 3}, // last code point below the surrogates
    ValidCase{.bytes = "\xEE\x80\x80",
              .codePoint = 0x00E000,
              .byteLength = 3}, // first code point above the surrogates
    ValidCase{
        .bytes = "\xEF\xBF\xBF", .codePoint = 0x00FFFF, .byteLength = 3}, // last 3-byte code point
    ValidCase{.bytes = "\xF0\x90\x80\x80",
              .codePoint = 0x010000,
              .byteLength = 4}, // first 4-byte (overlong boundary)
    ValidCase{
        .bytes = "\xF0\xBF\xBF\xBF", .codePoint = 0x03FFFF, .byteLength = 4}, // end of the F0 row
    ValidCase{
        .bytes = "\xF1\x80\x80\x80", .codePoint = 0x040000, .byteLength = 4}, // start of the F1 row
    ValidCase{
        .bytes = "\xF3\xBF\xBF\xBF", .codePoint = 0x0FFFFF, .byteLength = 4}, // end of the F3 row
    ValidCase{
        .bytes = "\xF4\x80\x80\x80", .codePoint = 0x100000, .byteLength = 4}, // start of the F4 row
    ValidCase{.bytes = "\xF4\x8F\xBF\xBF",
              .codePoint = 0x10FFFF,
              .byteLength = 4}, // last code point of Unicode
    ValidCase{.bytes = "\xF0\x9F\x98\x80", .codePoint = 0x01F600, .byteLength = 4}, // U+1F600 emoji
};

// One malformed case: a sequence whose FIRST code point is not well-formed
// (Table 3-7 rejection rows).
constexpr std::array<std::string_view, 16> INVALID_CASES{
    "\x80",             // stray continuation byte
    "\xBF",             // stray continuation byte (upper bound)
    "\xC0\x80",         // overlong encoding of U+0000 (2-byte)
    "\xC1\xBF",         // overlong encoding of U+007F (2-byte)
    "\xC2",             // truncated 2-byte sequence
    "\xE0\x80\x80",     // overlong encoding of U+0000 (3-byte)
    "\xE0\x9F\xBF",     // overlong encoding of U+07FF (3-byte)
    "\xE0\xA0",         // truncated 3-byte sequence
    "\xED\xA0\x80",     // surrogate U+D800
    "\xED\xBF\xBF",     // surrogate U+DFFF
    "\xED\x9F",         // truncated 3-byte sequence
    "\xF0\x80\x80\x80", // overlong encoding of U+0000 (4-byte)
    "\xF4\x90\x80\x80", // U+110000 - above U+10FFFF
    "\xF4\xBF\xBF\xBF", // U+13FFFF - above U+10FFFF
    "\xF5\x80\x80\x80", // lead byte above the F4 row
    "\xFF",             // invalid lead byte
};

// Checks that a result carries exactly the given CoreError. Isolated from
// CHECK so doctest never has to stringify a CoreError operand (ADL clash
// with infinity::core::toString, see error_test.cpp).
template <typename T>
[[nodiscard]] bool failsWith(infinity::core::CoreError expected,
                             const infinity::core::Expected<T>& result) noexcept {
    return !result.has_value() && result.error() == expected;
}

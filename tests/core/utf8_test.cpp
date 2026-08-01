// tests/core/utf8_test.cpp
//
// Contract tests for the UTF-8 decode/encode facility (F2.8a, ADR-023,
// rule 11): decoding follows Unicode Table 3-7 (well-formed sequences only -
// overlong encodings, surrogate code points, values above U+10FFFF, truncated
// sequences and stray continuation bytes are rejected as INVALID_UTF8, a
// recoverable invalid_argument error, rule 04), encoding is the exact inverse
// on the scalar-value range, and a fixed-seed corpus property-tests the
// round trip (ADR-013/017). Iteration and whole-string validation live in
// utf8_view_test.cpp.
//
// CI-safe: no time-based or environment-dependent assertions. The only
// randomness is std::minstd_rand with a fixed seed, whose parameters are
// mandated by the standard, so the sequence is identical on every platform.
#include "infinity/core/utf8.h"
#include "utf8_test_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <random>

#include <doctest/doctest.h>

TEST_CASE("decodeUtf8 decodes every well-formed sequence of Table 3-7") {
    for (const ValidCase& entry : VALID_CASES) {
        const infinity::core::Expected<infinity::core::Utf8Decoded> decoded =
            infinity::core::decodeUtf8(entry.bytes);
        CHECK(decoded.has_value());
        if (!decoded.has_value()) {
            continue;
        }
        CHECK(decoded->codePoint == entry.codePoint);
        CHECK(decoded->byteLength == entry.byteLength);
    }
}

TEST_CASE("decodeUtf8 rejects every malformed sequence of Table 3-7") {
    for (const std::string_view bytes : INVALID_CASES) {
        const infinity::core::Expected<infinity::core::Utf8Decoded> decoded =
            infinity::core::decodeUtf8(bytes);
        CHECK(failsWith(infinity::core::CoreError::INVALID_UTF8, decoded));
    }
}

TEST_CASE("decodeUtf8 of an empty sequence reports out_of_bounds") {
    const infinity::core::Expected<infinity::core::Utf8Decoded> decoded =
        infinity::core::decodeUtf8("");
    CHECK(failsWith(infinity::core::CoreError::OUT_OF_BOUNDS, decoded));
}

TEST_CASE("encodeUtf8 encodes every scalar value of the Table 3-7 range") {
    for (const ValidCase& entry : VALID_CASES) {
        const infinity::core::Expected<infinity::core::Utf8Encoded> encoded =
            infinity::core::encodeUtf8(entry.codePoint);
        CHECK(encoded.has_value());
        if (!encoded.has_value()) {
            continue;
        }
        CHECK(encoded->byteLength == entry.byteLength);
        CHECK(encoded->view() == entry.bytes);
    }
}

TEST_CASE("encodeUtf8 rejects surrogate and out-of-range code points") {
    constexpr std::array<char32_t, 4> NON_SCALARS{
        static_cast<char32_t>(0xD800),     // first surrogate code point
        static_cast<char32_t>(0xDFFF),     // last surrogate code point
        static_cast<char32_t>(0x110000),   // one past U+10FFFF
        static_cast<char32_t>(0xFFFFFFFF), // widest possible value
    };
    for (const char32_t scalar : NON_SCALARS) {
        const infinity::core::Expected<infinity::core::Utf8Encoded> encoded =
            infinity::core::encodeUtf8(scalar);
        CHECK(failsWith(infinity::core::CoreError::INVALID_UTF8, encoded));
    }
}

TEST_CASE("encode followed by decode round-trips the scalar value boundaries") {
    constexpr std::array<char32_t, 12> SCALAR_BOUNDARIES{
        0x000000, 0x000001, 0x00007F, 0x000080, 0x0007FF, 0x000800,
        0x00D7FF, 0x00E000, 0x00FFFF, 0x010000, 0x100000, 0x10FFFF,
    };
    for (const char32_t scalar : SCALAR_BOUNDARIES) {
        const infinity::core::Expected<infinity::core::Utf8Encoded> encoded =
            infinity::core::encodeUtf8(scalar);
        CHECK(encoded.has_value());
        if (!encoded.has_value()) {
            continue;
        }
        const infinity::core::Expected<infinity::core::Utf8Decoded> decoded =
            infinity::core::decodeUtf8(encoded->view());
        CHECK(decoded.has_value());
        if (!decoded.has_value()) {
            continue;
        }
        CHECK(decoded->codePoint == scalar);
        CHECK(decoded->byteLength == encoded->byteLength);
    }
}

TEST_CASE("encode followed by decode round-trips a deterministic corpus of code points") {
    // ADR-013/017 property test: std::minstd_rand is a linear_congruential_
    // engine with standard-mandated parameters, so a fixed seed produces the
    // same sequence on every platform and CI run. Surrogate draws fold into
    // the valid range, so every sample must round-trip.
    constexpr std::uint32_t FIXED_SEED = 0xF2A8;
    constexpr std::size_t SAMPLE_COUNT = 4096;
    std::minstd_rand rng(FIXED_SEED);

    std::size_t verified = 0;
    for (std::size_t i = 0; i < SAMPLE_COUNT; ++i) {
        const uint32_t raw = rng() % 0x110000;
        const uint32_t folded = (raw >= 0xD800 && raw <= 0xDFFF) ? (raw + 0x800) : raw;
        const auto scalar = static_cast<char32_t>(folded);

        const infinity::core::Expected<infinity::core::Utf8Encoded> encoded =
            infinity::core::encodeUtf8(scalar);
        CHECK(encoded.has_value());
        if (!encoded.has_value()) {
            continue;
        }
        const infinity::core::Expected<infinity::core::Utf8Decoded> decoded =
            infinity::core::decodeUtf8(encoded->view());
        CHECK(decoded.has_value());
        if (!decoded.has_value()) {
            continue;
        }
        CHECK(decoded->codePoint == scalar);
        CHECK(decoded->byteLength == encoded->byteLength);

        // Re-encoding the decoded scalar must reproduce the exact bytes:
        // decode is the inverse of encode over the whole scalar range.
        const infinity::core::Expected<infinity::core::Utf8Encoded> reEncoded =
            infinity::core::encodeUtf8(decoded->codePoint);
        CHECK(reEncoded.has_value());
        if (reEncoded.has_value()) {
            CHECK(reEncoded->view() == encoded->view());
        }
        ++verified;
    }
    CHECK(verified == SAMPLE_COUNT);
}

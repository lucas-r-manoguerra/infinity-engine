// tests/core/utf8_property_test.cpp
//
// Fuzz-style property tests for the UTF-8 facility (F2.9, ADR-017) driven by
// the property-testing harness: decodeUtf8 never crashes and always reports
// either a decoded sequence or a documented error on arbitrary byte slices,
// and encodeUtf8 round-trips random scalar values to the same bytes (rule 11:
// fixed seed, a failing property reports its exact case and seed).
//
// Complements utf8_test.cpp, whose boundary corpus and minstd_rand round-trip
// cover Table 3-7; this file feeds the full 0..0x10FFFF range and arbitrary
// byte streams through the mt19937 harness.
#include "infinity/core/testing/property_test.h"
#include "infinity/core/utf8.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

using infinity::core::CoreError;
using infinity::core::decodeUtf8;
using infinity::core::encodeUtf8;
using infinity::core::Expected;
using infinity::core::Utf8Decoded;
using infinity::core::Utf8Encoded;
using infinity::core::testing::bytes;
using infinity::core::testing::checkForAll;
using infinity::core::testing::i32;
using infinity::core::testing::PropertyRng;

TEST_CASE("decodeUtf8 never crashes on arbitrary byte slices") {
    // Fuzzing ADR-017: any byte stream, including the empty slice, must end in
    // either a well-formed sequence or a documented recoverable error (rule 04:
    // INVALID_UTF8 for malformed input, OUT_OF_BOUNDS for an empty slice).
    checkForAll(20260731u, 512, [](PropertyRng& rng) {
        const std::vector<std::uint8_t> buffer =
            bytes(rng, static_cast<std::size_t>(i32(rng, 0, 64)));
        const std::string_view input{reinterpret_cast<const char*>(buffer.data()), buffer.size()};

        const Expected<Utf8Decoded> decoded = decodeUtf8(input);
        if (!decoded.has_value()) {
            const CoreError error = decoded.error();
            return error == CoreError::INVALID_UTF8 || error == CoreError::OUT_OF_BOUNDS;
        }
        return decoded->byteLength >= 1 && decoded->byteLength <= 4 &&
               static_cast<std::size_t>(decoded->byteLength) <= input.size();
    });
}

TEST_CASE("encodeUtf8 round-trips random scalar values across the whole range") {
    // ADR-017 property: serialization round-trips to identity. Draw uniformly
    // from U+0000..U+10FFFF (folding surrogate draws into the valid range) and
    // verify decode(encode(s)) == s and that re-encoding reproduces the exact
    // bytes, including multi-byte sequences.
    checkForAll(20260801u, 512, [](PropertyRng& rng) {
        // NOLINTBEGIN(modernize-use-auto): rule 02 forbids auto for trivial types
        const std::uint32_t raw = static_cast<std::uint32_t>(i32(rng, 0, 0x10FFFF));
        const std::uint32_t folded = (raw >= 0xD800u && raw <= 0xDFFFu) ? (raw + 0x800u) : raw;
        const char32_t scalar = static_cast<char32_t>(folded);
        // NOLINTEND(modernize-use-auto)

        const Expected<Utf8Encoded> encoded = encodeUtf8(scalar);
        if (!encoded.has_value()) {
            return false;
        }

        const Expected<Utf8Decoded> decoded = decodeUtf8(encoded->view());
        if (!decoded.has_value() || decoded->codePoint != scalar ||
            decoded->byteLength != encoded->byteLength) {
            return false;
        }

        const Expected<Utf8Encoded> reEncoded = encodeUtf8(decoded->codePoint);
        return reEncoded.has_value() && reEncoded->view() == encoded->view();
    });
}

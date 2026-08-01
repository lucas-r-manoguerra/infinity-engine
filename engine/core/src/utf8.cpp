// infinity/core/src/utf8.cpp
//
// Implementation of the UTF-8 facility (F2.8a, ADR-023, rule 11): a direct,
// branch-bounded encoding of Unicode Table 3-7. No tables, no allocation, no
// platform dependence - each rejection row of the table maps to one range
// check on the lead byte (plus continuation checks), so the acceptance and
// rejection conditions are verifiable by inspection.
#include "infinity/core/utf8.h"

#include <cassert>

namespace infinity::core {
namespace {

// A continuation byte is 0b10xxxxxx (Table 3-7). The caller guarantees the
// byte exists before calling.
[[nodiscard]] constexpr bool isContinuation(char byte) noexcept {
    const auto value = static_cast<uint8_t>(byte);
    return (value & 0xC0) == 0x80;
}

} // namespace

Expected<Utf8Decoded> decodeUtf8(std::string_view input) noexcept {
    if (input.empty()) {
        return std::unexpected(CoreError::OUT_OF_BOUNDS);
    }

    const auto first = static_cast<uint8_t>(input[0]);
    std::size_t sequenceLength = 0;
    char32_t codePoint = 0;

    if (first <= 0x7F) {
        sequenceLength = 1;
        codePoint = first;
    } else if (first >= 0xC2 && first <= 0xDF) {
        sequenceLength = 2;
        codePoint = first & 0x1F;
    } else if (first >= 0xE0 && first <= 0xEF) {
        sequenceLength = 3;
        codePoint = first & 0x0F;
    } else if (first >= 0xF0 && first <= 0xF4) {
        sequenceLength = 4;
        codePoint = first & 0x07;
    } else {
        // 0x80..0xC1 (continuation bytes and overlong leads) and 0xF5..0xFF
        // (lead bytes above U+10FFFF) never start a sequence.
        return std::unexpected(CoreError::INVALID_UTF8);
    }

    if (input.size() < sequenceLength) {
        return std::unexpected(CoreError::INVALID_UTF8); // truncated sequence
    }
    for (std::size_t i = 1; i < sequenceLength; ++i) {
        if (!isContinuation(input[i])) {
            return std::unexpected(CoreError::INVALID_UTF8);
        }
        codePoint =
            static_cast<char32_t>((codePoint << 6) | (static_cast<uint8_t>(input[i]) & 0x3F));
    }

    // Table 3-7 second-byte constraints: the lead-byte range alone cannot
    // express overlong encodings, surrogate code points or values above
    // U+10FFFF. input[1] exists here (sequenceLength >= 2 implies
    // input.size() >= 2) and is a continuation byte.
    if (sequenceLength == 3 && first == 0xE0 && static_cast<uint8_t>(input[1]) < 0xA0) {
        return std::unexpected(CoreError::INVALID_UTF8); // overlong U+0000..U+07FF
    }
    if (sequenceLength == 3 && first == 0xED && static_cast<uint8_t>(input[1]) > 0x9F) {
        return std::unexpected(CoreError::INVALID_UTF8); // surrogate U+D800..U+DFFF
    }
    if (sequenceLength == 4 && first == 0xF0 && static_cast<uint8_t>(input[1]) < 0x90) {
        return std::unexpected(CoreError::INVALID_UTF8); // overlong U+0000..U+FFFF
    }
    if (sequenceLength == 4 && first == 0xF4 && static_cast<uint8_t>(input[1]) > 0x8F) {
        return std::unexpected(CoreError::INVALID_UTF8); // above U+10FFFF
    }

    return Utf8Decoded{.codePoint = codePoint, .byteLength = static_cast<uint8_t>(sequenceLength)};
}

Expected<Utf8Encoded> encodeUtf8(char32_t scalar) noexcept {
    const auto value = static_cast<uint32_t>(scalar);
    if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return std::unexpected(CoreError::INVALID_UTF8);
    }

    Utf8Encoded encoded;
    if (value <= 0x7F) {
        encoded.bytes[0] = static_cast<char>(value);
        encoded.byteLength = 1;
    } else if (value <= 0x7FF) {
        encoded.bytes[0] = static_cast<char>(0xC0 | (value >> 6));
        encoded.bytes[1] = static_cast<char>(0x80 | (value & 0x3F));
        encoded.byteLength = 2;
    } else if (value <= 0xFFFF) {
        encoded.bytes[0] = static_cast<char>(0xE0 | (value >> 12));
        encoded.bytes[1] = static_cast<char>(0x80 | ((value >> 6) & 0x3F));
        encoded.bytes[2] = static_cast<char>(0x80 | (value & 0x3F));
        encoded.byteLength = 3;
    } else {
        encoded.bytes[0] = static_cast<char>(0xF0 | (value >> 18));
        encoded.bytes[1] = static_cast<char>(0x80 | ((value >> 12) & 0x3F));
        encoded.bytes[2] = static_cast<char>(0x80 | ((value >> 6) & 0x3F));
        encoded.bytes[3] = static_cast<char>(0x80 | (value & 0x3F));
        encoded.byteLength = 4;
    }
    assert(encoded.byteLength >= 1 && encoded.byteLength <= 4);
    return encoded;
}

bool isValidUtf8(std::string_view input) noexcept {
    std::string_view remaining = input;
    while (!remaining.empty()) {
        const Expected<Utf8Decoded> decoded = decodeUtf8(remaining);
        if (!decoded.has_value()) {
            return false;
        }
        remaining.remove_prefix(decoded->byteLength);
    }
    return true;
}

} // namespace infinity::core

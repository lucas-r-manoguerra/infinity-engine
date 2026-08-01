// infinity/core/utf8.h
//
// UTF-8 facility (F2.8a, ADR-023, rule 11). ADR-023 makes UTF-8 the only
// encoding in the engine and keeps core free of OS knowledge, so this module
// is a pure, dependency-free implementation of the standard: decoding follows
// Unicode Table 3-7 (well-formed sequences only), every scalar value in
// U+0000..U+10FFFF encodes to its shortest form, and malformed input is
// reported as a recoverable error instead of being substituted (rule 04).
//
//   Errors     - Malformed sequences and non-scalar code points are recoverable
//                caller errors: INVALID_UTF8 (invalid_argument category,
//                ADR-003). decodeUtf8 of an empty slice reports OUT_OF_BOUNDS,
//                mirroring the container convention of the core vocabulary.
//   Allocation - None. Everything works on string_views and fixed-size
//                buffers, so the module is usable in frame hot paths (rule 03).
//   Determinism- No hidden state, no environment: identical input always
//                produces identical output on every platform (rule 11).
//
// Public API:
//   - decodeUtf8(std::string_view)          -> Expected<Utf8Decoded>
//   - encodeUtf8(char32_t)                  -> Expected<Utf8Encoded>
//   - isValidUtf8(std::string_view)         -> bool
//   - Utf8View: forward iteration over the code points of a string, reporting
//     each invalid byte as an error instead of substituting it.
#pragma once

#include "infinity/core/error.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace infinity::core {

// Result of decoding the first well-formed sequence of a byte slice.
struct Utf8Decoded {
    char32_t codePoint = 0; ///< the scalar value carried by the sequence
    uint8_t byteLength = 0; ///< number of bytes the sequence occupies (1..4)
};

// Result of encoding a scalar value to its shortest well-formed form. Bytes
// beyond byteLength are zero and are never part of view().
struct Utf8Encoded {
    std::array<char, 4> bytes{}; ///< the encoded sequence, shortest form
    uint8_t byteLength = 0;      ///< number of valid bytes in bytes (1..4)

    // The encoded sequence as a string_view. Never allocates and never copies
    // beyond the byteLength valid bytes.
    [[nodiscard]] std::string_view view() const noexcept { return {bytes.data(), byteLength}; }
};

// Decodes the first UTF-8 sequence of input (Unicode Table 3-7): the shortest
// well-formed encoding of a scalar value in U+0000..U+10FFFF, rejecting
// overlong encodings, surrogate code points, values above U+10FFFF, truncated
// sequences and stray continuation bytes as INVALID_UTF8.
//
// Errors (rule 04, all recoverable):
//   - INVALID_UTF8  - the first sequence is not well-formed
//   - OUT_OF_BOUNDS - input is empty, so no code point is available
[[nodiscard]] Expected<Utf8Decoded> decodeUtf8(std::string_view input) noexcept;

// Encodes a scalar value to its shortest well-formed UTF-8 form. Returns
// INVALID_UTF8 when the argument is not a Unicode scalar value (surrogate
// code points U+D800..U+DFFF and values above U+10FFFF, Table 3-7).
[[nodiscard]] Expected<Utf8Encoded> encodeUtf8(char32_t scalar) noexcept;

// Reports whether the whole slice is well-formed UTF-8 (empty input is
// trivially valid). Equivalent to a full decode walk; provided for the common
// "validate before use" pattern without forcing the caller to handle per-code
// point errors.
[[nodiscard]] bool isValidUtf8(std::string_view input) noexcept;

// Forward iteration over the code points of a string. Dereferencing yields
// Expected<Utf8Decoded>: a well-formed sequence decodes to its scalar value,
// an invalid byte is reported once as INVALID_UTF8 and the iterator advances
// one byte past it (resync), then iteration continues. No substitution ever
// happens (rule 04): errors are surfaced, never silently replaced.
class Utf8View {
public:
    explicit Utf8View(std::string_view input) noexcept : m_input(input) {}

    // Forward iterator over the code points, following the std::string_view
    // nested-iterator convention.
    class Iterator {
    public:
        // Dereferences to the decode result of the sequence at the current
        // position. Dereferencing end() is undefined behavior.
        [[nodiscard]] Expected<Utf8Decoded> operator*() const { return decodeUtf8(m_remaining); }

        // Advances to the next code point. On an invalid byte, advances one
        // byte past it (documented resync policy).
        Iterator& operator++() noexcept {
            const Expected<Utf8Decoded> decoded = decodeUtf8(m_remaining);
            if (decoded.has_value()) {
                m_remaining.remove_prefix(decoded->byteLength);
            } else if (!m_remaining.empty()) {
                m_remaining.remove_prefix(1);
            }
            return *this;
        }

        [[nodiscard]] bool operator==(const Iterator& other) const noexcept {
            const bool lhsDone = m_remaining.empty();
            const bool rhsDone = other.m_remaining.empty();
            if (lhsDone || rhsDone) {
                return lhsDone == rhsDone;
            }
            return m_remaining.data() == other.m_remaining.data() &&
                   m_remaining.size() == other.m_remaining.size();
        }

    private:
        friend class Utf8View;

        explicit Iterator() noexcept = default;
        explicit Iterator(std::string_view remaining) noexcept : m_remaining(remaining) {}

        std::string_view m_remaining;
    };

    [[nodiscard]] Iterator begin() const noexcept { return Iterator{m_input}; }
    // end() stays an instance member so callers and range-for use the idiomatic
    // view.end() form; a static end() would trip
    // readability-static-accessed-through-instance at every call site.
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    [[nodiscard]] Iterator end() const noexcept { return Iterator{}; }

private:
    std::string_view m_input;
};

} // namespace infinity::core

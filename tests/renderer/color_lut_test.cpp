// tests/renderer/color_lut_test.cpp
//
// Contract tests for the precomputed linear-to-sRGB lookup table (G2). The
// raster hot path converts exactly once per written pixel (F4.7, ADR-037) with
// a per-channel std::pow on the transcendental branch; the table replaces that
// with a single 1024-entry lookup per channel (linear input quantized to 10
// bits). These tests pin the table's contract: endpoint mapping, monotonicity,
// rounding coherence with the exact curve, and a bounded deviation guarantee.
#include "infinity/renderer/color.h"

#include <cstdint>

#include <doctest/doctest.h>

namespace {

using infinity::renderer::linearChannelToSrgb;
using infinity::renderer::srgbLookupTable;

// Mirror of the exact rounding contract used by the pixel pack path:
// byte = trunc(0.5 + 255 * channel), never floor/ceil.
[[nodiscard]] std::uint8_t exactSrgbByte(float linear) {
    // NOLINTNEXTLINE(bugprone-incorrect-roundings) -- contract: +0.5 then truncate.
    return static_cast<std::uint8_t>(0.5f + (255.0f * linearChannelToSrgb(linear)));
}

} // namespace

TEST_CASE("sRGB lookup table maps the endpoints exactly") {
    const auto& lut = srgbLookupTable();
    CHECK(lut[0] == 0u);
    CHECK(lut[1023] == 255u);
}

TEST_CASE("sRGB lookup table is monotonically non-decreasing") {
    const auto& lut = srgbLookupTable();
    for (std::size_t i = 0; i + 1 < lut.size(); ++i) {
        CHECK(lut[i] <= lut[i + 1]);
    }
}

TEST_CASE("sRGB lookup table matches the exact curve at every entry") {
    const auto& lut = srgbLookupTable();
    for (std::size_t i = 0; i < lut.size(); ++i) {
        const float linear = static_cast<float>(i) / 1023.0f;
        CHECK(lut[i] == exactSrgbByte(linear));
    }
}

TEST_CASE("sRGB lookup table never deviates from the exact curve by more than one byte") {
    const auto& lut = srgbLookupTable();
    // Deterministic sweep (rule 11): 257 linearly spaced probes including both
    // endpoints, no rand(). Probes are NOT table entries (they are exact
    // fractions of 256, not of 1023), so this bounds the 10-bit quantization
    // error of the table against the exact curve.
    for (int probe = 0; probe <= 256; ++probe) {
        const float linear = static_cast<float>(probe) / 256.0f;
        // NOLINTNEXTLINE(bugprone-incorrect-roundings) -- contract: +0.5 then truncate.
        const auto index = static_cast<std::size_t>(0.5f + (1023.0f * linear));
        const int deviation =
            static_cast<int>(lut[index]) - static_cast<int>(exactSrgbByte(linear));
        CAPTURE(probe);
        CHECK(deviation >= -1);
        CHECK(deviation <= 1);
    }
}

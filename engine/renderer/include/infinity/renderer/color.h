// infinity/renderer/color.h
//
// Linear-color vocabulary (F4.7, ADR-037). The renderer works in linear space:
// vertex colors are linear RGBA in [0, 1] and the linear-to-sRGB conversion
// happens exactly once, at present time, right before a value is written to an
// sRGB target. Working in linear space keeps lighting and interpolation
// physically correct (ADR-037); converting only at the end means the pipeline
// never round-trips a gamma-corrected value by accident.
//
// linearToSrgb implements the standard IEC 61966-2-1 piecewise curve, clamps
// out-of-range inputs to [0, 1] (a color outside the gamut is not representable
// on an sRGB target), and never touches alpha: gamma applies to RGB only.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace infinity::renderer {

// Linear RGBA color. Channels are linear floats in [0, 1] by contract; alpha
// is linear opacity. Aggregate, so brace-initializable and copyable.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

// Linear-to-sRGB conversion for one channel: clamps to [0, 1] and maps through
// the standard curve (c <= 0.0031308 ? 12.92 * c : 1.055 * c^(1/2.4) - 0.055).
[[nodiscard]] inline float linearChannelToSrgb(float linear) noexcept {
    const float c = std::clamp(linear, 0.0f, 1.0f);
    if (c >= 1.0f) {
        return 1.0f;
    }
    if (c <= 0.0031308f) {
        return c * 12.92f;
    }
    return (1.055f * std::pow(c, 1.0f / 2.4f)) - 0.055f;
}

// Converts a linear RGBA color to sRGB. Each RGB channel maps through the
// standard curve and is clamped to [0, 1]; alpha is returned unchanged.
[[nodiscard]] inline Color linearToSrgb(const Color& color) noexcept {
    return Color{.r = linearChannelToSrgb(color.r),
                 .g = linearChannelToSrgb(color.g),
                 .b = linearChannelToSrgb(color.b),
                 .a = color.a};
}

// Precomputed linear-to-sRGB lookup table (G2). Entry i maps linear value
// i/1023 to its sRGB byte using the exact curve above and the same rounding
// contract as the pixel pack path (trunc(0.5 + 255 * channel)). The 1024
// entries quantize the linear input to 10 bits, which never deviates from the
// exact curve by more than one byte (color_lut_test.cpp), while keeping the
// raster hot path to a single lookup per channel instead of a per-pixel
// std::pow. The returned table is const after first init.
[[nodiscard]] const std::array<std::uint8_t, 1024>& srgbLookupTable() noexcept;

} // namespace infinity::renderer

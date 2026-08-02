// tests/renderer/color_test.cpp
//
// Contract tests for the linear-to-sRGB conversion (F4.7, ADR-037): the renderer
// works in linear space and converts to sRGB only at present time. The mapping
// is the standard IEC 61966-2-1 piecewise curve, clamped to [0, 1] on input and
// output, and never touches alpha (sRGB is a gamma on RGB only).
#include "infinity/renderer/color.h"

#include <ostream>

#include <doctest/doctest.h>

namespace {

using infinity::renderer::Color;
using infinity::renderer::linearToSrgb;

// Tolerance for the transcendental piece of the curve: float pow() plus the
// 1/2.4 exponent can differ from the reference in the last ulps.
constexpr double SRGB_EPSILON = 0.0001;

} // namespace

TEST_CASE("linearToSrgb maps the curve anchors exactly") {
    CHECK(linearToSrgb(Color{0.0f, 0.0f, 0.0f, 1.0f}).r == 0.0f);
    CHECK(linearToSrgb(Color{0.0f, 0.0f, 0.0f, 1.0f}).g == 0.0f);
    CHECK(linearToSrgb(Color{0.0f, 0.0f, 0.0f, 1.0f}).b == 0.0f);
    CHECK(linearToSrgb(Color{1.0f, 1.0f, 1.0f, 1.0f}).r == 1.0f);
    CHECK(linearToSrgb(Color{1.0f, 1.0f, 1.0f, 1.0f}).g == 1.0f);
    CHECK(linearToSrgb(Color{1.0f, 1.0f, 1.0f, 1.0f}).b == 1.0f);
}

TEST_CASE("linearToSrgb matches the standard curve at a mid value") {
    // Reference: 1.055 * 0.5^(1/2.4) - 0.055 = 0.735357...
    const Color converted = linearToSrgb(Color{.r = 0.5f, .g = 0.5f, .b = 0.5f, .a = 1.0f});
    CHECK(converted.r == doctest::Approx(0.73536).epsilon(SRGB_EPSILON));
    CHECK(converted.g == doctest::Approx(0.73536).epsilon(SRGB_EPSILON));
    CHECK(converted.b == doctest::Approx(0.73536).epsilon(SRGB_EPSILON));
}

TEST_CASE("linearToSrgb is monotonic over the unit interval") {
    for (int step = 0; step < 20; ++step) {
        const float lowValue = static_cast<float>(step) * 0.05f;
        const float highValue = static_cast<float>(step + 1) * 0.05f;
        const Color low =
            linearToSrgb(Color{.r = lowValue, .g = lowValue, .b = lowValue, .a = 1.0f});
        const Color high =
            linearToSrgb(Color{.r = highValue, .g = highValue, .b = highValue, .a = 1.0f});
        CAPTURE(step);
        CHECK(low.r <= high.r);
        CHECK(low.g <= high.g);
        CHECK(low.b <= high.b);
    }
}

TEST_CASE("linearToSrgb leaves alpha untouched") {
    const Color converted = linearToSrgb(Color{.r = 0.5f, .g = 0.3f, .b = 0.9f, .a = 0.25f});
    CHECK(converted.a == 0.25f);
}

TEST_CASE("linearToSrgb clamps out-of-range linear inputs to the unit interval") {
    const Color overRange = linearToSrgb(Color{.r = 1.5f, .g = -0.5f, .b = 2.0f, .a = 1.0f});
    CHECK(overRange.r == 1.0f);
    CHECK(overRange.g == 0.0f);
    CHECK(overRange.b == 1.0f);
}

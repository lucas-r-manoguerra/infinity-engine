// tests/math/mat4_property_test.cpp
//
// Property tests for Mat4 (F2.9, ADR-017) driven by the property-testing
// harness: for well-conditioned random TRS matrices, inverted() round-trips to
// the identity and transpose and inverse commute. Fixed seeds (rule 11): a
// failing property reports its exact 0-based case and seed for reproduction.
#include "infinity/core/testing/property_test.h"
#include "infinity/math/mat4.h"

#include <cmath>

#include <doctest/doctest.h>

using infinity::core::testing::checkForAll;
using infinity::core::testing::f32;
using infinity::core::testing::PropertyRng;
using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

// Element-wise comparison within the absolute 1e-4 tolerance documented on
// Mat4::operator==.
bool matricesNear(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (!(std::abs(a.m[i] - b.m[i]) <= EPSILON)) {
            return false;
        }
    }
    return true;
}

// Random well-conditioned TRS matrix: positive scale bounded away from zero
// (never singular, rule 07 SRT order), arbitrary rotation, bounded
// translation. Built from the harness generators so the run is
// seed-reproducible (rule 11).
Mat4 randomWellConditioned(PropertyRng& rng) {
    const Vec3 position{f32(rng, -5.0f, 5.0f), f32(rng, -5.0f, 5.0f), f32(rng, -5.0f, 5.0f)};
    const Vec3 scale{f32(rng, 0.5f, 2.0f), f32(rng, 0.5f, 2.0f), f32(rng, 0.5f, 2.0f)};
    return Mat4::translation(position) *
           Mat4::rotationYawPitchRoll(f32(rng, -180.0f, 180.0f), f32(rng, -180.0f, 180.0f),
                                      f32(rng, -180.0f, 180.0f)) *
           Mat4::scale(scale);
}

} // namespace

TEST_CASE("Mat4 inverse round-trips to identity for random TRS matrices") {
    // ADR-017 property: inverse(M) * M == M * inverse(M) == I.
    checkForAll(20260731u, 200, [](PropertyRng& rng) {
        const Mat4 m = randomWellConditioned(rng);
        return matricesNear(m * m.inverted(), Mat4::identity()) &&
               matricesNear(m.inverted() * m, Mat4::identity());
    });
}

TEST_CASE("Mat4 transpose and inverse commute for random TRS matrices") {
    // ADR-017 property: transpose(inverse(M)) == inverse(transpose(M)).
    checkForAll(20260801u, 200, [](PropertyRng& rng) {
        const Mat4 m = randomWellConditioned(rng);
        return matricesNear(m.inverted().transposed(), m.transposed().inverted());
    });
}

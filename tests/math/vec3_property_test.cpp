// tests/math/vec3_property_test.cpp
//
// Property tests for Vec3 (F2.9, ADR-017): cross-product perpendicularity,
// normalization and dot-product symmetry for random vectors. Fixed seeds
// (rule 11): a failing property reports its exact 0-based case and seed for
// reproduction.
#include "infinity/core/testing/property_test.h"
#include "infinity/math/vec3.h"

#include <cmath>

#include <doctest/doctest.h>

using infinity::core::testing::checkForAll;
using infinity::core::testing::f32;
using infinity::core::testing::PropertyRng;
using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

Vec3 randomVec(PropertyRng& rng) {
    return Vec3{f32(rng, -5.0f, 5.0f), f32(rng, -5.0f, 5.0f), f32(rng, -5.0f, 5.0f)};
}

// Perpendicular within a tolerance that scales with the product of the input
// magnitudes: the residual of a dot product is float rounding noise, which
// grows with the operands rather than staying at a fixed absolute error.
bool perpendicular(const Vec3& a, const Vec3& b) {
    const float scale = 1.0f + (a.length() * b.length());
    return std::abs(a.dot(b)) <= EPSILON * scale;
}

} // namespace

TEST_CASE("Vec3 cross product is perpendicular to both inputs") {
    checkForAll(20260731u, 200, [](PropertyRng& rng) {
        const Vec3 a = randomVec(rng);
        const Vec3 b = randomVec(rng);
        const Vec3 c = a.cross(b);
        return perpendicular(a, c) && perpendicular(b, c);
    });
}

TEST_CASE("Vec3 normalize of non-zero vectors is unit length") {
    // The zero vector is the documented degenerate case and normalizes to
    // zero(); every other input must map to a finite unit vector (ADR-056).
    checkForAll(20260801u, 200, [](PropertyRng& rng) {
        const Vec3 v = randomVec(rng);
        if (v.lengthSquared() == 0.0f) {
            return true;
        }
        const Vec3 normalized = v.normalized();
        const float len = normalized.length();
        return std::isfinite(len) && std::abs(len - 1.0f) <= EPSILON;
    });
}

TEST_CASE("Vec3 dot product is symmetric") {
    // dot() evaluates (x*ox)+(y*oy)+(z*oz); both orders evaluate the same
    // products in the same sequence, so IEEE floating point makes the result
    // bit-identical (rule 11, no -ffast-math).
    checkForAll(20260802u, 200, [](PropertyRng& rng) {
        const Vec3 a = randomVec(rng);
        const Vec3 b = randomVec(rng);
        return a.dot(b) == b.dot(a);
    });
}

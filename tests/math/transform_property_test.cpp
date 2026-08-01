// tests/math/transform_property_test.cpp
//
// Property tests for Transform (F2.9, ADR-017): the derived matrix follows the
// SRT contract (rule 07) and child-parent composition keeps the TRS form and
// matches matrix composition. Fixed seeds (rule 11): a failing property
// reports its exact 0-based case and seed for reproduction.
#include "infinity/core/testing/property_test.h"
#include "infinity/math/transform.h"

#include <cmath>

#include <doctest/doctest.h>

using infinity::core::testing::checkForAll;
using infinity::core::testing::f32;
using infinity::core::testing::PropertyRng;
using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

bool matricesNear(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (!(std::abs(a.m[i] - b.m[i]) <= EPSILON)) {
            return false;
        }
    }
    return true;
}

bool vecsNear(const Vec3& a, const Vec3& b) {
    return std::abs(a.x - b.x) <= EPSILON && std::abs(a.y - b.y) <= EPSILON &&
           std::abs(a.z - b.z) <= EPSILON;
}

// q and -q encode the same rotation, so composition is compared up to sign.
bool quatsNearUpToSign(const Quat& a, const Quat& b) {
    const auto near = [](const Quat& lhs, const Quat& rhs) {
        return std::abs(lhs.x - rhs.x) <= EPSILON && std::abs(lhs.y - rhs.y) <= EPSILON &&
               std::abs(lhs.z - rhs.z) <= EPSILON && std::abs(lhs.w - rhs.w) <= EPSILON;
    };
    return near(a, b) || near(a, Quat{-b.x, -b.y, -b.z, -b.w});
}

// Random TRS transform (harness generators, seed-reproducible, rule 11).
Transform randomTransform(PropertyRng& rng) {
    return Transform{Vec3{f32(rng, -5.0f, 5.0f), f32(rng, -5.0f, 5.0f), f32(rng, -5.0f, 5.0f)},
                     Quat::fromYawPitchRoll(f32(rng, -180.0f, 180.0f), f32(rng, -180.0f, 180.0f),
                                            f32(rng, -180.0f, 180.0f)),
                     Vec3{f32(rng, 0.5f, 2.0f), f32(rng, 0.5f, 2.0f), f32(rng, 0.5f, 2.0f)}};
}

// Random transform whose scale is uniform on all axes.
Transform randomUniformScaleTransform(PropertyRng& rng) {
    const float scale = f32(rng, 0.5f, 2.0f);
    return Transform{Vec3{f32(rng, -5.0f, 5.0f), f32(rng, -5.0f, 5.0f), f32(rng, -5.0f, 5.0f)},
                     Quat::fromYawPitchRoll(f32(rng, -180.0f, 180.0f), f32(rng, -180.0f, 180.0f),
                                            f32(rng, -180.0f, 180.0f)),
                     Vec3{scale, scale, scale}};
}

} // namespace

TEST_CASE("Transform matrix equals the T*R*S product of its components") {
    // Rule 07 SRT order: M = T * R * S.
    checkForAll(20260731u, 200, [](PropertyRng& rng) {
        const Transform t = randomTransform(rng);
        const Mat4 expected =
            Mat4::translation(t.position) * t.rotation.toMat4() * Mat4::scale(t.scale);
        return matricesNear(t.matrix(), expected);
    });
}

TEST_CASE("Transform child-parent composition matches the TRS contract") {
    // With a uniform parent scale the composed TRS equals the raw matrix
    // product (rotation and scale commute); that is the documented exactness
    // condition on Transform::operator*. The component formulas hold for any
    // scale, so both are checked.
    checkForAll(20260801u, 200, [](PropertyRng& rng) {
        const Transform child = randomTransform(rng);
        const Transform parent = randomUniformScaleTransform(rng);
        const Transform combined = child * parent;

        const Vec3 expectedPosition =
            parent.position + (parent.rotation * Vec3{parent.scale.x * child.position.x,
                                                      parent.scale.y * child.position.y,
                                                      parent.scale.z * child.position.z});
        const Vec3 expectedScale =
            Vec3{parent.scale.x * child.scale.x, parent.scale.y * child.scale.y,
                 parent.scale.z * child.scale.z};

        return vecsNear(combined.position, expectedPosition) &&
               vecsNear(combined.scale, expectedScale) &&
               quatsNearUpToSign(combined.rotation, parent.rotation * child.rotation) &&
               matricesNear(combined.matrix(), parent.matrix() * child.matrix());
    });
}

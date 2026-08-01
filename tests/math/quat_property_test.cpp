// tests/math/quat_property_test.cpp
//
// Property tests for Quat (F2.9, ADR-017): normalization, slerp and
// composition of random unit quaternions stay on the unit hypersphere (no NaN
// or infinite values, ADR-056). Fixed seeds (rule 11): a failing property
// reports its exact 0-based case and seed for reproduction.
#include "infinity/core/testing/property_test.h"
#include "infinity/math/quat.h"

#include <cmath>

#include <doctest/doctest.h>

using infinity::core::testing::checkForAll;
using infinity::core::testing::f32;
using infinity::core::testing::PropertyRng;
using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

// Random unit quaternion from a random axis and angle (harness generators,
// seed-reproducible, rule 11). A zero axis is the documented degenerate case
// and yields the identity quaternion.
Quat randomUnitQuat(PropertyRng& rng) {
    const Vec3 axis{f32(rng, -1.0f, 1.0f), f32(rng, -1.0f, 1.0f), f32(rng, -1.0f, 1.0f)};
    return Quat::fromAxisAngle(axis, f32(rng, -180.0f, 180.0f));
}

bool nearUnit(const Quat& q) {
    const float len = q.length();
    return std::isfinite(len) && std::abs(len - 1.0f) <= EPSILON;
}

} // namespace

TEST_CASE("Quat normalize of random quaternions is unit length") {
    checkForAll(20260731u, 200, [](PropertyRng& rng) {
        const Quat raw{f32(rng, -2.0f, 2.0f), f32(rng, -2.0f, 2.0f), f32(rng, -2.0f, 2.0f),
                       f32(rng, -2.0f, 2.0f)};
        return nearUnit(raw.normalized());
    });
}

TEST_CASE("Quat slerp between random unit quaternions is unit length") {
    // The interpolant is a rotation for every t in [0, 1] (rule 07: slerp).
    checkForAll(20260801u, 200, [](PropertyRng& rng) {
        const Quat start = randomUnitQuat(rng);
        const Quat end = randomUnitQuat(rng);
        return nearUnit(start.slerp(end, f32(rng, 0.0f, 1.0f)));
    });
}

TEST_CASE("Quat composition of random rotations is unit length") {
    checkForAll(20260802u, 200, [](PropertyRng& rng) {
        const Quat combined = randomUnitQuat(rng) * randomUnitQuat(rng) * randomUnitQuat(rng);
        return nearUnit(combined);
    });
}

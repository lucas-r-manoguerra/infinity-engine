// tests/math/transform_test.cpp
#include "infinity/math/transform.h"

#include <cmath>
#include <random>

#include <doctest/doctest.h>

using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

// Approx with a tolerance suited to float transform math (relative 1e-4).
doctest::Approx near(float value) { return doctest::Approx(value).epsilon(1e-4); }

// Element-wise comparison of two matrices within the absolute 1e-4 tolerance.
bool matricesNear(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (!(std::abs(a.m[i] - b.m[i]) <= EPSILON)) {
            return false;
        }
    }
    return true;
}

// Component-wise comparison of two vectors within the absolute 1e-4 tolerance.
bool vecsNear(const Vec3& a, const Vec3& b) {
    return std::abs(a.x - b.x) <= EPSILON && std::abs(a.y - b.y) <= EPSILON &&
           std::abs(a.z - b.z) <= EPSILON;
}

// Random TRS transform (fixed seed supplied by the caller so property tests
// stay deterministic, ADR-017).
Transform randomTransform(std::mt19937& rng, bool uniformScale = false) {
    std::uniform_real_distribution<float> angle{-180.0f, 180.0f};
    std::uniform_real_distribution<float> position{-5.0f, 5.0f};
    std::uniform_real_distribution<float> scale{0.5f, 2.0f};
    const float s = scale(rng);
    const Vec3 scaleVec = uniformScale ? Vec3{s, s, s} : Vec3{scale(rng), scale(rng), scale(rng)};
    return Transform{Vec3{position(rng), position(rng), position(rng)},
                     Quat::fromYawPitchRoll(angle(rng), angle(rng), angle(rng)), scaleVec};
}

} // namespace

TEST_CASE("Transform identity has identity matrix and leaves points unchanged") {
    const Transform identity = Transform::identity();
    CHECK(matricesNear(identity.matrix(), Mat4::identity()));
    CHECK(identity.position.x == 0.0f);
    CHECK(identity.position.y == 0.0f);
    CHECK(identity.position.z == 0.0f);
    CHECK(identity.scale.x == 1.0f);
    CHECK(identity.scale.y == 1.0f);
    CHECK(identity.scale.z == 1.0f);

    const Vec3 p{1.0f, 2.0f, 3.0f};
    CHECK(vecsNear(identity.transformPoint(p), p));
    CHECK(vecsNear(identity.transformDirection(Vec3::right()), Vec3::right()));

    // A default-constructed Transform is also the identity transform.
    const Transform def;
    CHECK(matricesNear(def.matrix(), Mat4::identity()));
}

TEST_CASE("Transform matrix equals translation times rotation times scale") {
    // M = T * R * S (SRT order, rule 07): the matrix must match the product of
    // the Mat4 factories built from the same components.
    const Vec3 position{3.0f, -2.0f, 5.0f};
    const Quat rotation = Quat::fromYawPitchRoll(30.0f, -15.0f, 45.0f);
    const Vec3 scale{2.0f, 3.0f, 4.0f};
    const Transform t{position, rotation, scale};
    const Mat4 expected = Mat4::translation(position) * rotation.toMat4() * Mat4::scale(scale);
    CHECK(matricesNear(t.matrix(), expected));
}

TEST_CASE("Transform matrix applies scale then rotation then translation") {
    // SRT order verified step by step (rule 07): scale(2), rotate 90 deg about
    // +Z, translate (1, 0, 0); point (1, 0, 0).
    const Transform t{Vec3{1.0f, 0.0f, 0.0f}, Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f),
                      Vec3{2.0f, 2.0f, 2.0f}};
    const Vec3 p{1.0f, 0.0f, 0.0f};

    // Step 1: scale -> (2, 0, 0). Step 2: rotate +Z by 90 -> (0, 2, 0) (maps
    // +X toward +Y). Step 3: translate -> (1, 2, 0).
    const Vec3 scaled{2.0f, 0.0f, 0.0f};
    const Vec3 rotated = t.rotation * scaled;
    CHECK(rotated.x == near(0.0f));
    CHECK(rotated.y == near(2.0f));
    CHECK(rotated.z == near(0.0f));
    const Vec3 expected{t.position.x + rotated.x, t.position.y + rotated.y,
                        t.position.z + rotated.z};
    CHECK(expected.x == near(1.0f));
    CHECK(expected.y == near(2.0f));
    CHECK(expected.z == near(0.0f));

    const Vec3 actual = t.matrix() * p;
    CHECK(actual.x == near(expected.x));
    CHECK(actual.y == near(expected.y));
    CHECK(actual.z == near(expected.z));
}

TEST_CASE("Transform child parent composition applies child local then parent") {
    // child rotates 90 deg about +Z, parent translates by (1, 0, 0). A point
    // goes through the child first (rotated), then the parent (translated).
    const Transform child{Vec3::zero(), Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f),
                          Vec3::one()};
    const Transform parent{Vec3{1.0f, 0.0f, 0.0f}, Quat::identity(), Vec3::one()};
    const Transform combined = child * parent;

    // Parent has no rotation/scale, so the composed rotation is the child's
    // rotation and the composed position is the parent's position.
    CHECK(vecsNear(combined.position, Vec3{1.0f, 0.0f, 0.0f}));
    const Vec3 rotated = combined.rotation * Vec3::right();
    CHECK(rotated.x == near(0.0f));
    CHECK(rotated.y == near(1.0f));
    CHECK(rotated.z == near(0.0f));

    // Point (1, 0, 0): child rotates it to (0, 1, 0), parent translates to
    // (1, 1, 0).
    const Vec3 p{1.0f, 0.0f, 0.0f};
    const Vec3 result = combined.transformPoint(p);
    CHECK(result.x == near(1.0f));
    CHECK(result.y == near(1.0f));
    CHECK(result.z == near(0.0f));
}

TEST_CASE("Transform composition combines position through parent scale and rotation") {
    // The composed position must be
    // parent.position + parent.rotation * (parent.scale * child.position),
    // and the composed scale the component-wise product (rule 07, SRT).
    const Transform child{Vec3{1.0f, 0.0f, 0.0f}, Quat::identity(), Vec3{2.0f, 2.0f, 2.0f}};
    const Transform parent{Vec3{0.0f, 0.0f, 0.0f},
                           Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f),
                           Vec3{3.0f, 3.0f, 3.0f}};
    const Transform combined = child * parent;

    // scale: 3 * 2 per axis. rotation: parent applied after child.
    CHECK(vecsNear(combined.scale, Vec3{6.0f, 6.0f, 6.0f}));
    // position: parent.rotation * (parent.scale * child.position) =
    // Rz90 * (3, 0, 0) = (0, 3, 0).
    CHECK(vecsNear(combined.position, Vec3{0.0f, 3.0f, 0.0f}));

    // Point (1, 0, 0): child scales to (2, 0, 0) and translates to (3, 0, 0);
    // parent scales to (9, 0, 0), rotates to (0, 9, 0), no translation.
    CHECK(vecsNear(combined.transformPoint(Vec3{1.0f, 0.0f, 0.0f}), Vec3{0.0f, 9.0f, 0.0f}));
}

TEST_CASE("Transform transformDirection ignores translation and scale") {
    const Transform t{Vec3{10.0f, 10.0f, 10.0f}, Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f),
                      Vec3{3.0f, 3.0f, 3.0f}};
    // Only the rotation applies: +X is rotated toward +Y, length preserved.
    const Vec3 d = t.transformDirection(Vec3::right());
    CHECK(d.x == near(0.0f));
    CHECK(d.y == near(1.0f));
    CHECK(d.z == near(0.0f));
    CHECK(d.length() == near(1.0f));
}

TEST_CASE("Transform transformPoint applies full TRS") {
    const Transform t{Vec3{1.0f, 2.0f, 3.0f}, Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f),
                      Vec3{2.0f, 2.0f, 2.0f}};
    const Vec3 p{1.0f, 1.0f, 1.0f};
    // scale -> (2, 2, 2); rotate +Z 90 -> (-2, 2, 2); translate -> (-1, 4, 5).
    const Vec3 actual = t.transformPoint(p);
    CHECK(actual.x == near(-1.0f));
    CHECK(actual.y == near(4.0f));
    CHECK(actual.z == near(5.0f));
    // transformPoint must agree with the full matrix product.
    CHECK(vecsNear(actual, t.matrix() * p));
}

TEST_CASE("Transform matrix round trip with composed transform") {
    // child * parent applies child first, then parent, so its matrix is
    // matrix(parent) * matrix(child) (rule 07: `a * b` applies b first).
    // With uniform parent scale the TRS composition is exact, so the composed
    // Transform's matrix must reproduce that product.
    const Transform child{Vec3{2.0f, 1.0f, -1.0f}, Quat::fromYawPitchRoll(30.0f, -45.0f, 60.0f),
                          Vec3{2.0f, 3.0f, 4.0f}};
    const Transform parent{Vec3{-1.0f, 4.0f, 2.0f}, Quat::fromYawPitchRoll(-20.0f, 35.0f, 15.0f),
                           Vec3{2.0f, 2.0f, 2.0f}};
    const Transform combined = child * parent;
    CHECK(matricesNear(combined.matrix(), parent.matrix() * child.matrix()));
}

TEST_CASE("Transform composition round trip matches matrix product for uniform parent scale") {
    // Property test (ADR-017): with a uniform parent scale the composed TRS is
    // exactly the matrix product matrix(parent) * matrix(child), fixed seed.
    std::mt19937 rng{20260207u};
    int checked = 0;
    for (int i = 0; i < 50; ++i) {
        const Transform child = randomTransform(rng);
        const Transform parent = randomTransform(rng, /*uniformScale=*/true);
        const Transform combined = child * parent;
        CHECK(matricesNear(combined.matrix(), parent.matrix() * child.matrix()));
        // The composed transform must transform points the same way.
        std::uniform_real_distribution<float> comp{-5.0f, 5.0f};
        const Vec3 p{comp(rng), comp(rng), comp(rng)};
        CHECK(vecsNear(combined.transformPoint(p), parent.matrix() * (child.matrix() * p)));
        ++checked;
    }
    CHECK(checked == 50);
}

TEST_CASE("Transform composing with identity yields the other transform") {
    const Transform t{Vec3{2.0f, -1.0f, 3.0f}, Quat::fromYawPitchRoll(40.0f, -25.0f, 70.0f),
                      Vec3{2.0f, 3.0f, 4.0f}};
    // identity * t applies t first, then nothing: result is t.
    CHECK(matricesNear((t * Transform::identity()).matrix(), t.matrix()));
    CHECK(vecsNear((t * Transform::identity()).position, t.position));
    // t * identity applies nothing, then t: result is t.
    CHECK(matricesNear((Transform::identity() * t).matrix(), t.matrix()));
}

TEST_CASE("Transform zero scale collapses points onto the position") {
    const Transform t{Vec3{1.0f, 2.0f, 3.0f}, Quat::fromYawPitchRoll(30.0f, 45.0f, -60.0f),
                      Vec3::zero()};
    const Vec3 p{4.0f, 5.0f, 6.0f};
    const Vec3 result = t.transformPoint(p);
    CHECK(result.x == near(1.0f));
    CHECK(result.y == near(2.0f));
    CHECK(result.z == near(3.0f));
    // The matrix agrees: zero scale zeroes the upper-left 3x3.
    CHECK(vecsNear(result, t.matrix() * p));
    // Direction is unaffected by scale (rotation still applies).
    CHECK(vecsNear(t.transformDirection(Vec3::right()), t.rotation * Vec3::right()));
}

TEST_CASE("Transform negative scale reflects points") {
    const Transform t{Vec3{1.0f, 1.0f, 1.0f}, Quat::identity(), Vec3{-2.0f, 2.0f, 2.0f}};
    const Vec3 p{1.0f, 1.0f, 1.0f};
    // scale -> (-2, 2, 2); no rotation; translate -> (-1, 3, 3).
    const Vec3 result = t.transformPoint(p);
    CHECK(result.x == near(-1.0f));
    CHECK(result.y == near(3.0f));
    CHECK(result.z == near(3.0f));
    CHECK(vecsNear(result, t.matrix() * p));
}

TEST_CASE("Transform 360 degree rotation equals identity rotation") {
    const Transform t{Vec3::zero(), Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 360.0f),
                      Vec3::one()};
    const Vec3 p{1.0f, 2.0f, 3.0f};
    CHECK(vecsNear(t.transformPoint(p), p));
    CHECK(matricesNear(t.matrix(), Mat4::identity()));
}

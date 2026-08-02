// tests/math/quat_basic_test.cpp
//
// Quat core contract tests (F1, rule 07): identity, fromAxisAngle with the
// right-handed +Z -> +Y convention, normalize (property test + in-place +
// zero-quaternion determinism policy, ADR-056), compose order, inverse and
// conjugate, and the 4D dot product. The slerp cases live in
// quat_slerp_test.cpp and the matrix round-trip cases in quat_matrix_test.cpp
// (rule 01: One File = One Task).
#include "infinity/math/quat.h"

#include <cmath>
#include <random>

#include <doctest/doctest.h>

using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

// Approx with a tolerance suited to float quaternion math (relative 1e-4).
doctest::Approx near(float value) { return doctest::Approx(value).epsilon(1e-4); }

// Element-wise comparison of two quaternions within the absolute 1e-4
// tolerance (matches the epsilon documented on Quat::operator==).
bool quatsNear(const Quat& a, const Quat& b) {
    return std::abs(a.x - b.x) <= EPSILON && std::abs(a.y - b.y) <= EPSILON &&
           std::abs(a.z - b.z) <= EPSILON && std::abs(a.w - b.w) <= EPSILON;
}

// quatsNear, accepting q and -q as equivalent (they encode the same rotation).
bool quatsNearUpToSign(const Quat& a, const Quat& b) {
    return quatsNear(a, b) || quatsNear(a, Quat{-b.x, -b.y, -b.z, -b.w});
}

// Element-wise comparison of two matrices within the absolute 1e-4 tolerance.
bool matricesNear(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (!(std::abs(a.m[i] - b.m[i]) <= EPSILON)) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("Quat identity rotates nothing") {
    const Quat q = Quat::identity();
    CHECK(q.x == 0.0f);
    CHECK(q.y == 0.0f);
    CHECK(q.z == 0.0f);
    CHECK(q.w == 1.0f);
    const Vec3 v{1.0f, 2.0f, 3.0f};
    const Vec3 r = q * v;
    CHECK(r.x == near(v.x));
    CHECK(r.y == near(v.y));
    CHECK(r.z == near(v.z));
}

TEST_CASE("Quat from axis angle rotates vector 90 degrees") {
    // Right-handed convention (rule 07): a positive rotation about +Z maps
    // +X toward +Y.
    const Quat q = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f);
    const Vec3 r = q * Vec3::right();
    CHECK(r.x == near(0.0f));
    CHECK(r.y == near(1.0f));
    CHECK(r.z == near(0.0f));
    CHECK(q.length() == near(1.0f));
}

TEST_CASE("Quat normalize yields unit length") {
    // Property test (ADR-017): normalization always produces a unit-length
    // quaternion regardless of input magnitude, fixed seed (rule 06).
    std::mt19937 rng{20260204u};
    std::uniform_real_distribution<float> comp{-2.0f, 2.0f};
    int checked = 0;
    for (int i = 0; i < 100; ++i) {
        const Quat raw{comp(rng), comp(rng), comp(rng), comp(rng)};
        const Quat q = raw.normalized();
        CHECK(q.lengthSquared() == near(1.0f));
        CHECK(std::isfinite(q.length()));
        ++checked;
    }
    CHECK(checked == 100);
}

TEST_CASE("Quat normalize in place produces unit length") {
    Quat q{3.0f, 0.0f, 0.0f, 4.0f};
    q.normalize();
    CHECK(q.x == near(0.6f));
    CHECK(q.y == near(0.0f));
    CHECK(q.z == near(0.0f));
    CHECK(q.w == near(0.8f));
    CHECK(q.length() == near(1.0f));
}

TEST_CASE("Quat normalize of zero quaternion returns identity") {
    // Determinism policy (ADR-056): the zero quaternion has no direction;
    // normalization yields identity instead of NaN/Inf.
    const Quat zero{0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(zero.lengthSquared() == 0.0f);
    CHECK(quatsNear(zero.normalized(), Quat::identity()));
}

TEST_CASE("Quat compose applies rotations in order") {
    // q1 * q2 applies q2 first, then q1: (q1 * q2) * v == q1 * (q2 * v).
    const Quat q1 = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f);
    const Quat q2 = Quat::fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, 45.0f);
    const Vec3 v{1.0f, 0.0f, 0.0f};
    const Vec3 expected = q1 * (q2 * v);
    const Vec3 actual = (q1 * q2) * v;
    CHECK(actual.x == near(expected.x));
    CHECK(actual.y == near(expected.y));
    CHECK(actual.z == near(expected.z));
    // Quaternion composition is consistent with matrix composition.
    CHECK(matricesNear((q1 * q2).toMat4(), q1.toMat4() * q2.toMat4()));
    // Composition is not commutative.
    CHECK(!quatsNear(q1 * q2, q2 * q1));
}

TEST_CASE("Quat inverse rotates back") {
    const Quat q = Quat::fromYawPitchRoll(30.0f, 45.0f, -60.0f);
    CHECK(quatsNearUpToSign(q * q.inverse(), Quat::identity()));
    CHECK(quatsNearUpToSign(q.inverse() * q, Quat::identity()));
    // The inverse rotates vectors back to the start.
    const Vec3 v{1.0f, 2.0f, 3.0f};
    const Vec3 back = q.inverse() * (q * v);
    CHECK(back.x == near(v.x));
    CHECK(back.y == near(v.y));
    CHECK(back.z == near(v.z));
    // For a unit quaternion the inverse is the conjugate.
    CHECK(quatsNear(q.inverse(), q.conjugate()));
}

TEST_CASE("Quat conjugate negates the rotation axis") {
    const Quat q = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 60.0f);
    CHECK(quatsNear(q.conjugate().conjugate(), q));
    // Conjugating twice is the identity, so conjugating once reverses the
    // rotation: it rotates the same axis by the opposite angle.
    const Vec3 fwd = q * Vec3::right();
    const Vec3 back = q.conjugate() * Vec3::right();
    CHECK(fwd.x == near(std::cos(60.0f * 0.01745329252f)));
    CHECK(fwd.y == near(std::sin(60.0f * 0.01745329252f)));
    CHECK(back.x == near(std::cos(60.0f * 0.01745329252f)));
    CHECK(back.y == near(-std::sin(60.0f * 0.01745329252f)));
}

TEST_CASE("Quat dot product follows the 4D inner product") {
    const Quat a{1.0f, 2.0f, 3.0f, 4.0f};
    const Quat b{5.0f, -1.0f, 0.0f, 2.0f};
    CHECK(a.dot(b) == near(11.0f));
    CHECK(a.dot(a) == near(a.lengthSquared()));
    CHECK(a.dot(b) == near(b.dot(a)));
}

TEST_CASE("Quat from axis angle with 360 degrees rotates nothing") {
    const Quat q = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 360.0f);
    CHECK(q.length() == near(1.0f));
    const Vec3 v{1.0f, 2.0f, 3.0f};
    const Vec3 r = q * v;
    CHECK(r.x == near(v.x));
    CHECK(r.y == near(v.y));
    CHECK(r.z == near(v.z));
    // q == (0, ~0, 0, -1): identity up to sign (same rotation).
    CHECK(quatsNearUpToSign(q, Quat::identity()));
}

TEST_CASE("Quat from axis angle with negative axis rotates the other way") {
    const Quat qPlus = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f);
    const Quat qMinus = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, -1.0f}, 90.0f);
    const Vec3 plus = qPlus * Vec3::right();
    const Vec3 minus = qMinus * Vec3::right();
    CHECK(plus.y == near(1.0f));
    CHECK(minus.y == near(-1.0f));
    // Opposite rotations cancel to identity.
    CHECK(quatsNearUpToSign(qPlus * qMinus, Quat::identity()));
}

TEST_CASE("Quat operator== and operator!= compare within epsilon") {
    Quat a = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f);
    Quat b = a;
    b.x += 0.00005f;
    CHECK(a == b);
    b.x += 0.001f;
    CHECK(a != b);
    CHECK(!(a == b));
}

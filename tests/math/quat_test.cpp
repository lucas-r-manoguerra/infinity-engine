// tests/math/quat_test.cpp
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

// Uniform random unit quaternion from a random axis and angle (fixed seed
// supplied by the caller so property tests stay deterministic, ADR-017).
Quat randomQuat(std::mt19937& rng) {
    std::uniform_real_distribution<float> angle{-180.0f, 180.0f};
    std::uniform_real_distribution<float> unit{-1.0f, 1.0f};
    const Vec3 axis{unit(rng), unit(rng), unit(rng)};
    return Quat::fromAxisAngle(axis, angle(rng));
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

TEST_CASE("Quat slerp at t=0 is start and t=1 is end") {
    const Quat start = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 30.0f);
    const Quat end = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 120.0f);
    CHECK(quatsNear(start.slerp(end, 0.0f), start));
    CHECK(quatsNear(start.slerp(end, 1.0f), end));
}

TEST_CASE("Quat slerp at t=0.5 is halfway rotation") {
    const Quat start = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 0.0f);
    const Quat end = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f);
    const Quat mid = start.slerp(end, 0.5f);
    CHECK(mid.length() == near(1.0f));
    // Halfway is a 45 degree rotation about +Z.
    const Vec3 r = mid * Vec3::right();
    CHECK(r.x == near(std::cos(45.0f * 0.01745329252f)));
    CHECK(r.y == near(std::sin(45.0f * 0.01745329252f)));
    CHECK(r.z == near(0.0f));
}

TEST_CASE("Quat slerp across 180 degrees does not produce NaN") {
    // Determinism policy (ADR-056): slerp with |dot| close to 1 (identical or
    // antipodal quaternions) uses normalized lerp so the result is always
    // finite — a plain slerp would divide by sin(acos(1)) == 0.
    const Quat q = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f);
    const Quat minusQ{-q.x, -q.y, -q.z, -q.w};
    CHECK(std::abs(q.dot(minusQ)) > 0.9995f);
    int checked = 0;
    for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
        const Quat r = q.slerp(minusQ, t);
        CHECK(std::isfinite(r.x));
        CHECK(std::isfinite(r.y));
        CHECK(std::isfinite(r.z));
        CHECK(std::isfinite(r.w));
        CHECK(r.length() == near(1.0f));
        ++checked;
    }
    CHECK(checked == 5);
}

TEST_CASE("Quat slerp takes the shorter path") {
    // Policy: when the inputs are more than 90 degrees apart on the 4D sphere
    // (dot < 0) slerp negates the end quaternion and interpolates the short
    // way around the sphere.
    const Quat start = Quat::identity();
    const Quat q90 = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, 90.0f);
    const Quat end{-q90.x, -q90.y, -q90.z, -q90.w};
    CHECK(start.dot(end) < 0.0f);
    const Quat mid = start.slerp(end, 0.5f);
    const Vec3 r = mid * Vec3::right();
    // The short path is +45 degrees about +Z (not the -135 degree long way).
    CHECK(r.x == near(std::cos(45.0f * 0.01745329252f)));
    CHECK(r.y == near(std::sin(45.0f * 0.01745329252f)));
    CHECK(r.z == near(0.0f));
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

TEST_CASE("Quat fromYawPitchRoll matches matrix rotation") {
    // YXZ order (rule 07): R = Ry(yaw) * Rx(pitch) * Rz(roll).
    const float yaw = 30.0f;
    const float pitch = -15.0f;
    const float roll = 45.0f;
    const Quat q = Quat::fromYawPitchRoll(yaw, pitch, roll);
    const Mat4 m = Mat4::rotationYawPitchRoll(yaw, pitch, roll);
    CHECK(matricesNear(q.toMat4(), m));
    CHECK(quatsNearUpToSign(Quat::fromMat4(m), q));
    // Composing the three single-axis quaternions in YXZ order matches.
    const Quat yawQ = Quat::fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yaw);
    const Quat pitchQ = Quat::fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, pitch);
    const Quat rollQ = Quat::fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, roll);
    CHECK(quatsNearUpToSign(yawQ * pitchQ * rollQ, q));
}

TEST_CASE("Quat to matrix round trip") {
    // Property test (ADR-017): fromMat4(toMat4(q)) recovers q for random
    // rotations, fixed seed (rule 06).
    std::mt19937 rng{20260205u};
    int checked = 0;
    for (int i = 0; i < 100; ++i) {
        const Quat q = randomQuat(rng);
        const Mat4 m = q.toMat4();
        // toMat4 yields a proper rotation: orthonormal and volume-preserving.
        CHECK(m.determinant() == near(1.0f));
        CHECK(matricesNear(m.transposed() * m, Mat4::identity()));
        CHECK(quatsNearUpToSign(Quat::fromMat4(m), q));
        ++checked;
    }
    CHECK(checked == 100);
}

TEST_CASE("Quat rotating a vector preserves its length") {
    // Property test (ADR-017): a rotation never changes vector magnitude.
    std::mt19937 rng{20260206u};
    std::uniform_real_distribution<float> comp{-5.0f, 5.0f};
    int checked = 0;
    for (int i = 0; i < 100; ++i) {
        const Quat q = randomQuat(rng);
        const Vec3 v{comp(rng), comp(rng), comp(rng)};
        const Vec3 r = q * v;
        CHECK(r.length() == near(v.length()));
        ++checked;
    }
    CHECK(checked == 100);
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

TEST_CASE("Quat fromMat4 extracts rotation from a translate-scale-rotate matrix") {
    const Vec3 t{10.0f, -5.0f, 3.0f};
    const Vec3 s{2.0f, 3.0f, 4.0f};
    const Mat4 trs =
        Mat4::translation(t) * Mat4::rotationYawPitchRoll(40.0f, -25.0f, 70.0f) * Mat4::scale(s);
    const Quat expected = Quat::fromYawPitchRoll(40.0f, -25.0f, 70.0f);
    CHECK(quatsNearUpToSign(Quat::fromMat4(trs), expected));
}

TEST_CASE("Quat fromMat4 of a degenerate matrix returns identity") {
    // Determinism policy (ADR-056): a matrix with a zero-length axis column
    // has no extractable rotation; fromMat4 returns identity instead of
    // NaN/Inf.
    const Mat4 zero;
    CHECK(quatsNear(Quat::fromMat4(zero), Quat::identity()));
    const Mat4 zeroScale = Mat4::scale(Vec3::zero());
    CHECK(quatsNear(Quat::fromMat4(zeroScale), Quat::identity()));
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

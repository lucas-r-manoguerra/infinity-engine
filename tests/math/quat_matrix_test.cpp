// tests/math/quat_matrix_test.cpp
//
// Quat <-> Mat4 CONVERSION contract tests (F1, rule 07): fromYawPitchRoll
// matches the matrix YXZ composition, toMat4 yields a proper rotation and
// fromMat4 round-trips it (fixed-seed property test, ADR-017), rotating a
// vector preserves length, fromMat4 extracts the rotation from a
// translate-scale-rotate matrix, and fromMat4 of a degenerate matrix returns
// identity (ADR-056 determinism policy). The core cases live in
// quat_basic_test.cpp and the slerp cases in quat_slerp_test.cpp (rule 01:
// One File = One Task).
//
// This file uses Mat4 directly, so it includes infinity/math/mat4.h
// explicitly: quat.h happens to pull it in transitively, but a test TU must
// never rely on transitive includes (rule 02: headers are self-contained).
#include "infinity/math/mat4.h"
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

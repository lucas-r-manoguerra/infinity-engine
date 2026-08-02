// tests/math/quat_slerp_test.cpp
//
// Quat SLERP contract tests (F1, ADR-056, rule 07): endpoint correctness
// (t=0 start, t=1 end), halfway interpolation with unit length, the
// anti-podal 180-degree determinism policy (normalized lerp instead of
// NaN/Inf), and the shorter-path policy when inputs are more than 90 degrees
// apart on the 4D sphere. The core cases live in quat_basic_test.cpp and the
// matrix round-trip cases in quat_matrix_test.cpp (rule 01: One File = One
// Task).
#include "infinity/math/quat.h"

#include <cmath>

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

} // namespace

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
    for (int i = 0; i <= 4; ++i) {
        const float t = static_cast<float>(i) * 0.25f;
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

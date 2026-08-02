// tests/math/mat4_transform_test.cpp
//
// Mat4 construction and transform composition contract tests (F1, rule 07):
// zero/identity initialization, translation, SRT multiplication order (scale
// then rotation then translation, rule 07), non-commutativity, single-axis
// rotation conventions (right-handed, positive +Z maps +X toward +Y), YXZ
// yaw/pitch/roll composition, and the epsilon-based operator==/operator!=.
// The inverse/determinant/property cases live in mat4_inverse_test.cpp and the
// projection cases in mat4_projection_test.cpp (rule 01: One File = One Task).
#include "infinity/math/mat4.h"

#include <cmath>

#include <doctest/doctest.h>

using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

// Approx with a tolerance suited to float matrix math (relative 1e-4).
doctest::Approx near(float value) { return doctest::Approx(value).epsilon(1e-4); }

// Element-wise comparison of two matrices within the relative 1e-4 tolerance.
bool matricesNear(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (!(std::abs(a.m[i] - b.m[i]) <= EPSILON)) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("Mat4 default constructor zero-initializes all 16 elements") {
    const Mat4 m;
    for (int i = 0; i < 16; ++i) {
        CHECK(m.m[i] == 0.0f);
    }
}

TEST_CASE("Mat4 identity has ones on the diagonal and zeros elsewhere") {
    const Mat4 i = Mat4::identity();
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            CHECK(i.m[(col * 4) + row] == (row == col ? 1.0f : 0.0f));
        }
    }
}

TEST_CASE("Mat4 identity leaves Vec4 and Vec3 unchanged") {
    const Vec4 v4{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 r4 = Mat4::identity() * v4;
    CHECK(r4.x == v4.x);
    CHECK(r4.y == v4.y);
    CHECK(r4.z == v4.z);
    CHECK(r4.w == v4.w);

    const Vec3 v3{1.0f, 2.0f, 3.0f};
    const Vec3 r3 = Mat4::identity() * v3;
    CHECK(r3.x == v3.x);
    CHECK(r3.y == v3.y);
    CHECK(r3.z == v3.z);
}

TEST_CASE("Mat4 translation moves a point by the translation vector") {
    const Vec3 t{3.0f, -2.0f, 5.0f};
    const Vec3 p{1.0f, 1.0f, 1.0f};
    const Vec3 r = Mat4::translation(t) * p;
    CHECK(r.x == near(4.0f));
    CHECK(r.y == near(-1.0f));
    CHECK(r.z == near(6.0f));
}

TEST_CASE("Mat4 multiplication composes transforms in order") {
    // result = a * b means "apply b first, then a". (T * R) * p must equal
    // T * (R * p): rotate the point first, then translate it.
    const Mat4 r = Mat4::rotationZ(90.0f);
    const Mat4 t = Mat4::translation(Vec3{1.0f, 2.0f, 3.0f});
    const Vec3 p{2.0f, 0.0f, 0.0f};

    const Vec3 rotated = r * p;
    CHECK(rotated.x == near(0.0f));
    CHECK(rotated.y == near(2.0f));
    CHECK(rotated.z == near(0.0f));

    const Vec3 expected = t * rotated;
    const Vec3 actual = (t * r) * p;
    CHECK(actual.x == near(expected.x));
    CHECK(actual.y == near(expected.y));
    CHECK(actual.z == near(expected.z));
    CHECK(actual.x == near(1.0f));
    CHECK(actual.y == near(4.0f));
    CHECK(actual.z == near(3.0f));
}

TEST_CASE("Mat4 SRT multiplication applies scale then rotation then translation") {
    const Vec3 t{1.0f, 2.0f, 3.0f};
    const Mat4 s = Mat4::scale(Vec3{2.0f, 3.0f, 4.0f});
    const Mat4 r = Mat4::rotationZ(90.0f);
    const Mat4 m = Mat4::translation(t) * r * s;
    const Vec3 p{1.0f, 1.0f, 1.0f};
    const Vec3 actual = m * p;
    const Vec3 expected = Vec3{1.0f, 2.0f, 3.0f} + (r * (s * p));
    CHECK(actual.x == near(expected.x));
    CHECK(actual.y == near(expected.y));
    CHECK(actual.z == near(expected.z));
    // s * p = (2, 3, 4); rotationZ(90) -> (-3, 2, 4); + translation -> (-2, 4, 7)
    CHECK(actual.x == near(-2.0f));
    CHECK(actual.y == near(4.0f));
    CHECK(actual.z == near(7.0f));
}

TEST_CASE("Mat4 multiplication is non-commutative") {
    const Mat4 t = Mat4::translation(Vec3{1.0f, 0.0f, 0.0f});
    const Mat4 r = Mat4::rotationZ(90.0f);
    CHECK(!(t * r == r * t));
}

TEST_CASE("Mat4 rotationZ rotates the x-axis toward the y-axis") {
    // Right-handed convention: a positive rotation about +Z maps +X toward +Y.
    const Vec3 r = Mat4::rotationZ(90.0f) * Vec3::right();
    CHECK(r.x == near(0.0f));
    CHECK(r.y == near(1.0f));
    CHECK(r.z == near(0.0f));
}

TEST_CASE("Mat4 rotation at zero and full turns equals identity") {
    CHECK(matricesNear(Mat4::rotationX(0.0f), Mat4::identity()));
    CHECK(matricesNear(Mat4::rotationY(0.0f), Mat4::identity()));
    CHECK(matricesNear(Mat4::rotationZ(0.0f), Mat4::identity()));
    CHECK(matricesNear(Mat4::rotationX(360.0f), Mat4::identity()));
    CHECK(matricesNear(Mat4::rotationY(-360.0f), Mat4::identity()));
    CHECK(matricesNear(Mat4::rotationZ(720.0f), Mat4::identity()));
}

TEST_CASE("Mat4 rotationYawPitchRoll composes yaw pitch roll in YXZ order") {
    const float yaw = 30.0f;
    const float pitch = -15.0f;
    const float roll = 45.0f;
    const Mat4 composed = Mat4::rotationYawPitchRoll(yaw, pitch, roll);
    const Mat4 expected = Mat4::rotationY(yaw) * Mat4::rotationX(pitch) * Mat4::rotationZ(roll);
    CHECK(matricesNear(composed, expected));
}

TEST_CASE("Mat4 operator== and operator!= compare within epsilon") {
    Mat4 a = Mat4::identity();
    Mat4 b = Mat4::identity();
    b.m[5] += 0.00005f;
    CHECK(a == b);
    b.m[5] += 0.001f;
    CHECK(a != b);
    CHECK(!(a == b));
}

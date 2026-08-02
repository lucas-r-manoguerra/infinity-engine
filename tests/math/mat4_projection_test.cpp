// tests/math/mat4_projection_test.cpp
//
// Mat4 PROJECTION contract tests (F1, ADR-004/079, rule 07): perspective depth
// mapping to 0..1 (near plane -> 0, far plane -> 1) in the Vulkan/GL clip
// space convention, inside-frustum points staying in clip range, ortho bounds
// mapping to clip space, and the Vec3 (affine, w=1, no division) vs Vec4
// (full fourth row) multiplication split. The construction/transform cases
// live in mat4_transform_test.cpp and the inverse/determinant cases in
// mat4_inverse_test.cpp (rule 01: One File = One Task).
#include "infinity/math/mat4.h"

#include <cmath>

#include <doctest/doctest.h>

using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

// Approx with a tolerance suited to float matrix math (relative 1e-4).
doctest::Approx near(float value) { return doctest::Approx(value).epsilon(1e-4); }

bool nearZero(float value) { return std::abs(value) < EPSILON; }

} // namespace

TEST_CASE("Mat4 perspective projects a centered point to depth 0..1") {
    const Mat4 proj = Mat4::perspective(45.0f, 1.5f, 1.0f, 10.0f);
    const Vec4 clip = proj * Vec4{0.0f, 0.0f, -2.0f, 1.0f};
    const float ndcZ = clip.z / clip.w;
    CHECK(clip.x == near(0.0f));
    CHECK(clip.y == near(0.0f));
    CHECK(clip.w == near(2.0f));
    CHECK(ndcZ == near(5.0f / 9.0f));
    CHECK(ndcZ >= 0.0f);
    CHECK(ndcZ <= 1.0f);
}

TEST_CASE("Mat4 perspective maps near plane to depth 0 and far plane to depth 1") {
    const Mat4 proj = Mat4::perspective(45.0f, 1.5f, 1.0f, 10.0f);
    const Vec4 nearClip = proj * Vec4{0.0f, 0.0f, -1.0f, 1.0f};
    CHECK(nearZero(nearClip.z));
    const Vec4 farClip = proj * Vec4{0.0f, 0.0f, -10.0f, 1.0f};
    CHECK(farClip.z / farClip.w == near(1.0f));
}

TEST_CASE("Mat4 perspective keeps inside-frustum points in clip range") {
    const Mat4 proj = Mat4::perspective(45.0f, 1.5f, 1.0f, 10.0f);
    const Vec4 clip = proj * Vec4{0.6f, 0.4f, -2.0f, 1.0f};
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    CHECK(ndcX > -1.0f);
    CHECK(ndcX < 1.0f);
    CHECK(ndcY > -1.0f);
    CHECK(ndcY < 1.0f);
}

TEST_CASE("Mat4 ortho maps bounds to clip space") {
    const Mat4 ortho = Mat4::ortho(-2.0f, 2.0f, -1.0f, 1.0f, 1.0f, 10.0f);
    const Vec4 corner = ortho * Vec4{-2.0f, -1.0f, -1.0f, 1.0f};
    CHECK(corner.x == near(-1.0f));
    CHECK(corner.y == near(-1.0f));
    CHECK(corner.z == near(0.0f));
    const Vec4 farCorner = ortho * Vec4{2.0f, 1.0f, -10.0f, 1.0f};
    CHECK(farCorner.x == near(1.0f));
    CHECK(farCorner.y == near(1.0f));
    CHECK(farCorner.z == near(1.0f));
    const Vec4 mid = ortho * Vec4{0.0f, 0.0f, -5.5f, 1.0f};
    CHECK(mid.z == near(0.5f));
}

TEST_CASE("Mat4 multiplied by Vec3 assumes affine w=1 and does not divide by w") {
    const Mat4 proj = Mat4::perspective(45.0f, 1.5f, 1.0f, 10.0f);
    const Vec4 clip = proj * Vec4{0.0f, 0.0f, -2.0f, 1.0f};
    const Vec3 noDivision = proj * Vec3{0.0f, 0.0f, -2.0f};
    CHECK(clip.w == near(2.0f));
    CHECK(noDivision.x == near(clip.x));
    CHECK(noDivision.y == near(clip.y));
    CHECK(noDivision.z == near(clip.z));
}

TEST_CASE("Mat4 multiplied by Vec4 applies the full fourth row") {
    Mat4 m = Mat4::identity();
    m.m[15] = 2.0f;
    const Vec4 r = m * Vec4{1.0f, 2.0f, 3.0f, 4.0f};
    CHECK(r.x == near(1.0f));
    CHECK(r.y == near(2.0f));
    CHECK(r.z == near(3.0f));
    CHECK(r.w == near(8.0f));
}

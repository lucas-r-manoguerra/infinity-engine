// tests/math/mat4_test.cpp
#include "infinity/math/mat4.h"

#include <cmath>
#include <random>
#include <utility>

#include <doctest/doctest.h>

using namespace infinity::math;

namespace {

constexpr float EPSILON = 1e-4f;

// Approx with a tolerance suited to float matrix math (relative 1e-4).
doctest::Approx near(float value) { return doctest::Approx(value).epsilon(1e-4); }

bool nearZero(float value) { return std::abs(value) < EPSILON; }

// Element-wise comparison of two matrices within the relative 1e-4 tolerance.
bool matricesNear(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (!(std::abs(a.m[i] - b.m[i]) <= EPSILON)) {
            return false;
        }
    }
    return true;
}

// Element-wise comparison with an explicit tolerance: the allowed error is
// `tolerance * max(1, |a|, |b|)`, so both near-zero and large elements compare
// fairly. Used by the cross-implementation inverse tests.
bool matricesNear(const Mat4& a, const Mat4& b, float tolerance) {
    for (int i = 0; i < 16; ++i) {
        const float scale = std::max(1.0f, std::max(std::abs(a.m[i]), std::abs(b.m[i])));
        if (!(std::abs(a.m[i] - b.m[i]) <= tolerance * scale)) {
            return false;
        }
    }
    return true;
}

// Golden reference: the previous production inverse (Gauss-Jordan elimination
// with partial pivoting). Kept here so the new inverse is validated against an
// independent implementation (rule 06: each public API has a behavior test).
Mat4 gaussJordanInverse(const Mat4& input) {
    Mat4 aug = input;
    Mat4 inv = Mat4::identity();

    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        float best = std::abs(aug.m[col * 4 + col]);
        for (int row = col + 1; row < 4; ++row) {
            const float candidate = std::abs(aug.m[col * 4 + row]);
            if (candidate > best) {
                best = candidate;
                pivot = row;
            }
        }
        if (best == 0.0f) {
            return Mat4::identity();
        }
        if (pivot != col) {
            for (int k = 0; k < 4; ++k) {
                std::swap(aug.m[k * 4 + col], aug.m[k * 4 + pivot]);
                std::swap(inv.m[k * 4 + col], inv.m[k * 4 + pivot]);
            }
        }
        const float pivotValue = aug.m[col * 4 + col];
        for (int k = 0; k < 4; ++k) {
            aug.m[k * 4 + col] /= pivotValue;
            inv.m[k * 4 + col] /= pivotValue;
        }
        for (int row = 0; row < 4; ++row) {
            if (row == col) {
                continue;
            }
            const float factor = aug.m[col * 4 + row];
            if (factor == 0.0f) {
                continue;
            }
            for (int k = 0; k < 4; ++k) {
                aug.m[k * 4 + row] -= factor * aug.m[k * 4 + col];
                inv.m[k * 4 + row] -= factor * inv.m[k * 4 + col];
            }
        }
    }
    return inv;
}

// Random rigid-ish transform (translation * rotation * positive scale).
Mat4 randomTransform(std::mt19937& rng) {
    std::uniform_real_distribution<float> angle{-180.0f, 180.0f};
    std::uniform_real_distribution<float> position{-5.0f, 5.0f};
    std::uniform_real_distribution<float> scale{0.5f, 2.0f};
    return Mat4::translation(Vec3{position(rng), position(rng), position(rng)}) *
           Mat4::rotationYawPitchRoll(angle(rng), angle(rng), angle(rng)) *
           Mat4::scale(Vec3{scale(rng), scale(rng), scale(rng)});
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
            CHECK(i.m[col * 4 + row] == (row == col ? 1.0f : 0.0f));
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

TEST_CASE("Mat4 inverse restores the original transform") {
    const Mat4 m = Mat4::translation(Vec3{4.0f, -3.0f, 2.0f}) *
                   Mat4::rotationYawPitchRoll(30.0f, 45.0f, -60.0f);
    CHECK(matricesNear(m * m.inverted(), Mat4::identity()));
    CHECK(matricesNear(m.inverted() * m, Mat4::identity()));
}

TEST_CASE("Mat4 inverse of a translation is the opposite translation") {
    const Vec3 t{4.0f, -3.0f, 2.0f};
    const Mat4 m = Mat4::translation(t);
    CHECK(matricesNear(m.inverted(), Mat4::translation(-t)));
    CHECK(matricesNear(m * m.inverted(), Mat4::identity()));
}

TEST_CASE("Mat4 transposed of a rotation is its inverse") {
    const Mat4 r = Mat4::rotationYawPitchRoll(20.0f, -35.0f, 60.0f);
    CHECK(matricesNear(r.transposed(), r.inverted()));
    CHECK(matricesNear(r.transposed() * r, Mat4::identity()));
    CHECK(matricesNear(r * r.transposed(), Mat4::identity()));
}

TEST_CASE("Mat4 transposed twice is the original matrix") {
    const Mat4 m = Mat4::translation(Vec3{1.0f, 2.0f, 3.0f}) * Mat4::rotationX(45.0f) *
                   Mat4::scale(Vec3{2.0f, 1.0f, 0.5f});
    CHECK(matricesNear(m.transposed().transposed(), m));
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

TEST_CASE("Mat4 determinant of identity is 1") {
    CHECK(Mat4::identity().determinant() == near(1.0f));
}

TEST_CASE("Mat4 determinant of a scale matrix is the product of its scales") {
    const Mat4 s = Mat4::scale(Vec3{2.0f, 3.0f, 4.0f});
    CHECK(s.determinant() == near(24.0f));
}

TEST_CASE("Mat4 negative scale produces a negative determinant") {
    const Mat4 s = Mat4::scale(Vec3{-2.0f, 1.0f, 1.0f});
    CHECK(s.determinant() == near(-2.0f));
    CHECK(matricesNear(s * s.inverted(), Mat4::identity()));
}

TEST_CASE("Mat4 inverse of a singular matrix returns identity") {
    // Determinism policy (ADR-056): a singular matrix (exact zero pivot) has
    // no inverse; inverted() returns identity instead of NaN/Inf.
    const Mat4 zero;
    CHECK(zero.determinant() == 0.0f);
    CHECK(zero.inverted() == Mat4::identity());

    Mat4 singular = Mat4::identity();
    singular.m[5] = 0.0f;
    CHECK(singular.determinant() == 0.0f);
    CHECK(singular.inverted() == Mat4::identity());
}

TEST_CASE("Mat4 inverse of a zero-scale matrix returns identity") {
    const Mat4 m = Mat4::scale(Vec3::zero());
    CHECK(m.determinant() == 0.0f);
    CHECK(m.inverted() == Mat4::identity());
}

TEST_CASE("Mat4 inverse of a perspective matrix restores the identity") {
    // A perspective matrix has last row [0 0 -1 0], not [0 0 0 1], so it
    // misses the affine fast path and exercises the general cofactor branch.
    // Its inverse must still satisfy M * M^-1 = M^-1 * M = I.
    const Mat4 proj = Mat4::perspective(45.0f, 1.5f, 1.0f, 10.0f);
    CHECK(matricesNear(proj * proj.inverted(), Mat4::identity()));
    CHECK(matricesNear(proj.inverted() * proj, Mat4::identity()));
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

TEST_CASE("Mat4 operator== and operator!= compare within epsilon") {
    Mat4 a = Mat4::identity();
    Mat4 b = Mat4::identity();
    b.m[5] += 0.00005f;
    CHECK(a == b);
    b.m[5] += 0.001f;
    CHECK(a != b);
    CHECK(!(a == b));
}

TEST_CASE("Mat4 inverse of a product equals the reversed product of inverses") {
    // Property test (ADR-017): (A*B)^-1 == B^-1 * A^-1, fixed seed (rule 06).
    std::mt19937 rng{20260201u};
    int checked = 0;
    for (int i = 0; i < 50; ++i) {
        const Mat4 a = randomTransform(rng);
        const Mat4 b = randomTransform(rng);
        const Mat4 lhs = (a * b).inverted();
        const Mat4 rhs = b.inverted() * a.inverted();
        CHECK(matricesNear(lhs, rhs));
        ++checked;
    }
    CHECK(checked == 50);
}

TEST_CASE("Mat4 inverse matches the Gauss-Jordan reference for random transforms") {
    // Property test (ADR-017): the inverse must agree with the independent
    // Gauss-Jordan reference to 1e-3 relative for well-conditioned matrices.
    // Fixed seed (rule 06); 100 iterations pin the drop-in replacement.
    std::mt19937 rng{20260204u};
    int checked = 0;
    for (int i = 0; i < 100; ++i) {
        const Mat4 m = randomTransform(rng);
        CHECK(matricesNear(m.inverted(), gaussJordanInverse(m), 1e-3f));
        ++checked;
    }
    CHECK(checked == 100);
}

TEST_CASE("Mat4 inverse matches the Gauss-Jordan reference for random general matrices") {
    // Property test (ADR-017): random diagonally dominant matrices (diagonal in
    // [0.8, 1.2], off-diagonal in [-0.1, 0.1]) are provably well-conditioned
    // (Gershgorin: eigenvalues in [0.5, 1.5]) and exercise the full general
    // cofactor path, not just the affine TRS subset. Fixed seed; 100 iterations.
    std::mt19937 rng{20260205u};
    std::uniform_real_distribution<float> diag{0.8f, 1.2f};
    std::uniform_real_distribution<float> offDiag{-0.1f, 0.1f};
    int checked = 0;
    for (int i = 0; i < 100; ++i) {
        Mat4 m;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                m.m[col * 4 + row] = row == col ? diag(rng) : offDiag(rng);
            }
        }
        CHECK(matricesNear(m.inverted(), gaussJordanInverse(m), 1e-3f));
        CHECK(matricesNear(m * m.inverted(), Mat4::identity()));
        CHECK(matricesNear(m.inverted() * m, Mat4::identity()));
        ++checked;
    }
    CHECK(checked == 100);
}

TEST_CASE("Mat4 determinant of a product equals the product of determinants") {
    // Property test (ADR-017): det(A*B) == det(A)*det(B), fixed seed.
    std::mt19937 rng{20260202u};
    int checked = 0;
    for (int i = 0; i < 50; ++i) {
        const Mat4 a = randomTransform(rng);
        const Mat4 b = randomTransform(rng);
        const float detAB = (a * b).determinant();
        const float detA = a.determinant();
        const float detB = b.determinant();
        CHECK(detAB == near(detA * detB));
        CHECK(std::isfinite(detAB));
        ++checked;
    }
    CHECK(checked == 50);
}

TEST_CASE("Mat4 random rotation matrices have determinant one") {
    // Property test (ADR-017): rotations preserve volume, fixed seed.
    std::mt19937 rng{20260203u};
    std::uniform_real_distribution<float> angle{-180.0f, 180.0f};
    int checked = 0;
    for (int i = 0; i < 50; ++i) {
        const Mat4 r = Mat4::rotationYawPitchRoll(angle(rng), angle(rng), angle(rng));
        CHECK(r.determinant() == near(1.0f));
        ++checked;
    }
    CHECK(checked == 50);
}

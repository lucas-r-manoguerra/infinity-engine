// src/mat4.cpp
#include "infinity/math/mat4.h"

#include <array>
#include <cmath>
#include <numbers>

namespace infinity::math {

namespace {

constexpr float DEG_TO_RAD = std::numbers::pi_v<float> / 180.0f;

// Absolute epsilon used by operator== (documented in the header).
constexpr float MAT4_EPSILON = 1e-4f;

// Singularity threshold for inverted() (documented in the header, ADR-056):
// a matrix with |det| below this is treated as having no inverse.
constexpr float INVERSE_EPSILON = 1e-12f;

// Determinant of a 3x3 matrix stored row-major in a flat 9-float array.
float det3(const std::array<float, 9>& a) {
    return (a[0] * (a[4] * a[8] - a[5] * a[7])) - (a[1] * (a[3] * a[8] - a[5] * a[6])) +
           (a[2] * (a[3] * a[7] - a[4] * a[6]));
}

// Copies the 3x3 minor of `m` (column-major, m[col * 4 + row]) obtained by
// removing the given column and row into the row-major output `out`.
void minor3x3(const Mat4& m, int excludedCol, int excludedRow, std::array<float, 9>& out) {
    int i = 0;
    for (int row = 0; row < 4; ++row) {
        if (row == excludedRow) {
            continue;
        }
        for (int col = 0; col < 4; ++col) {
            if (col == excludedCol) {
                continue;
            }
            out[i] = m.m[(col * 4) + row];
            ++i;
        }
    }
}

// Affine fast path (rule 08): inverts a matrix whose last row is exactly
// [0 0 0 1] as the 3x3 cofactor/adjugate of the upper-left block plus the
// negated scaled translation — the inverse of [M3 t; 0 1] is [M3^-1 -M3^-1*t;
// 0 1]. Roughly half the work of the general 4x4 path below. Returns
// identity() for a singular 3x3 block (|det| < INVERSE_EPSILON), per ADR-056.
Mat4 invertAffine(const Mat4& m) {
    const float m00 = m.m[0];
    const float m10 = m.m[1];
    const float m20 = m.m[2];
    const float m01 = m.m[4];
    const float m11 = m.m[5];
    const float m21 = m.m[6];
    const float m02 = m.m[8];
    const float m12 = m.m[9];
    const float m22 = m.m[10];
    const float tx = m.m[12];
    const float ty = m.m[13];
    const float tz = m.m[14];

    const float a = (m00 * m11) - (m01 * m10);
    const float b = (m00 * m12) - (m02 * m10);
    const float c = (m01 * m12) - (m02 * m11);

    const float det = (m20 * c) - (m21 * b) + (m22 * a);
    if (std::abs(det) < INVERSE_EPSILON) {
        return Mat4::identity();
    }
    const float invDet = 1.0f / det;

    Mat4 result;
    result.m[0] = (m11 * m22 - m12 * m21) * invDet;
    result.m[1] = (m12 * m20 - m10 * m22) * invDet;
    result.m[2] = (m10 * m21 - m11 * m20) * invDet;
    result.m[4] = (m02 * m21 - m01 * m22) * invDet;
    result.m[5] = (m00 * m22 - m02 * m20) * invDet;
    result.m[6] = (m01 * m20 - m00 * m21) * invDet;
    result.m[8] = c * invDet;
    result.m[9] = -b * invDet;
    result.m[10] = a * invDet;
    result.m[12] = -((result.m[0] * tx) + (result.m[4] * ty) + (result.m[8] * tz));
    result.m[13] = -((result.m[1] * tx) + (result.m[5] * ty) + (result.m[9] * tz));
    result.m[14] = -((result.m[2] * tx) + (result.m[6] * ty) + (result.m[10] * tz));
    result.m[15] = 1.0f;
    return result;
}

} // namespace

Mat4 Mat4::identity() noexcept {
    Mat4 result;
    result.m[0] = 1.0f;
    result.m[5] = 1.0f;
    result.m[10] = 1.0f;
    result.m[15] = 1.0f;
    return result;
}

Mat4 Mat4::translation(const Vec3& translation) noexcept {
    Mat4 result = identity();
    result.m[12] = translation.x;
    result.m[13] = translation.y;
    result.m[14] = translation.z;
    return result;
}

Mat4 Mat4::rotationX(float degrees) noexcept {
    Mat4 result = identity();
    const float c = std::cos(degrees * DEG_TO_RAD);
    const float s = std::sin(degrees * DEG_TO_RAD);
    result.m[5] = c;
    result.m[6] = s;
    result.m[9] = -s;
    result.m[10] = c;
    return result;
}

Mat4 Mat4::rotationY(float degrees) noexcept {
    Mat4 result = identity();
    const float c = std::cos(degrees * DEG_TO_RAD);
    const float s = std::sin(degrees * DEG_TO_RAD);
    result.m[0] = c;
    result.m[2] = -s;
    result.m[8] = s;
    result.m[10] = c;
    return result;
}

Mat4 Mat4::rotationZ(float degrees) noexcept {
    Mat4 result = identity();
    const float c = std::cos(degrees * DEG_TO_RAD);
    const float s = std::sin(degrees * DEG_TO_RAD);
    result.m[0] = c;
    result.m[1] = s;
    result.m[4] = -s;
    result.m[5] = c;
    return result;
}

Mat4 Mat4::rotationYawPitchRoll(float yawDegrees, float pitchDegrees, float rollDegrees) noexcept {
    return rotationY(yawDegrees) * rotationX(pitchDegrees) * rotationZ(rollDegrees);
}

Mat4 Mat4::scale(const Vec3& scale) noexcept {
    Mat4 result = identity();
    result.m[0] = scale.x;
    result.m[5] = scale.y;
    result.m[10] = scale.z;
    return result;
}

Mat4 Mat4::perspective(float fovYDegrees, float aspect, float near, float far) noexcept {
    Mat4 result;
    const float f = 1.0f / std::tan(fovYDegrees * DEG_TO_RAD * 0.5f);
    result.m[0] = f / aspect;
    result.m[5] = f;
    result.m[10] = far / (near - far);
    result.m[11] = -1.0f;
    result.m[14] = (far * near) / (near - far);
    return result;
}

Mat4 Mat4::ortho(float left, float right, float bottom, float top, float near, float far) noexcept {
    Mat4 result = identity();
    result.m[0] = 2.0f / (right - left);
    result.m[5] = 2.0f / (top - bottom);
    result.m[10] = 1.0f / (near - far);
    result.m[12] = -(right + left) / (right - left);
    result.m[13] = -(top + bottom) / (top - bottom);
    result.m[14] = near / (near - far);
    return result;
}

Mat4 Mat4::operator*(const Mat4& other) const noexcept {
    Mat4 result;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            result.m[(col * 4) + row] = m[(0 * 4) + row] * other.m[(col * 4) + 0] +
                                        m[(1 * 4) + row] * other.m[(col * 4) + 1] +
                                        m[(2 * 4) + row] * other.m[(col * 4) + 2] +
                                        m[(3 * 4) + row] * other.m[(col * 4) + 3];
        }
    }
    return result;
}

Mat4 Mat4::transposed() const noexcept {
    Mat4 result;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            result.m[(col * 4) + row] = m[(row * 4) + col];
        }
    }
    return result;
}

Mat4 Mat4::inverted() const noexcept {
    // Affine matrices (last row exactly [0 0 0 1]) take the ~2x cheaper 3x3
    // fast path; perspective and general matrices fall through to the full
    // 4x4 cofactor inverse below. The affine test is exact on purpose: a
    // matrix that merely approximates affine still takes the general path
    // (correct, just slower).
    if (m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f && m[15] == 1.0f) {
        return invertAffine(*this);
    }
    // General cofactor/adjugate inverse: M^-1 = adj(M) / det(M), via the
    // "double cross-product" expansion — one scalar division and ~78
    // multiplies instead of the old Gauss-Jordan's ~32 scalar divisions
    // (rule 08). Singular (|det| < INVERSE_EPSILON) returns identity() and
    // never NaN/Inf (ADR-056). Inputs containing NaN/Inf are undefined.
    const float m00 = m[0];
    const float m10 = m[1];
    const float m20 = m[2];
    const float m30 = m[3];
    const float m01 = m[4];
    const float m11 = m[5];
    const float m21 = m[6];
    const float m31 = m[7];
    const float m02 = m[8];
    const float m12 = m[9];
    const float m22 = m[10];
    const float m32 = m[11];
    const float m03 = m[12];
    const float m13 = m[13];
    const float m23 = m[14];
    const float m33 = m[15];

    // 2x2 minors of the top and bottom 2x2 row pairs. Each minor appears in two
    // cofactors, so computing them once halves the multiply count.
    const float a0 = (m00 * m11) - (m01 * m10);
    const float a1 = (m00 * m12) - (m02 * m10);
    const float a2 = (m00 * m13) - (m03 * m10);
    const float a3 = (m01 * m12) - (m02 * m11);
    const float a4 = (m01 * m13) - (m03 * m11);
    const float a5 = (m02 * m13) - (m03 * m12);
    const float b0 = (m20 * m31) - (m21 * m30);
    const float b1 = (m20 * m32) - (m22 * m30);
    const float b2 = (m20 * m33) - (m23 * m30);
    const float b3 = (m21 * m32) - (m22 * m31);
    const float b4 = (m21 * m33) - (m23 * m31);
    const float b5 = (m22 * m33) - (m23 * m32);

    const float det = (a0 * b5) - (a1 * b4) + (a2 * b3) + (a3 * b2) - (a4 * b1) + (a5 * b0);
    if (std::abs(det) < INVERSE_EPSILON) {
        return identity();
    }
    const float invDet = 1.0f / det;

    // Adjugate scaled by 1/det, written directly in column-major order: the
    // element at (row, col) of the inverse is stored at m[col * 4 + row].
    Mat4 result;
    result.m[0] = (m11 * b5 - m12 * b4 + m13 * b3) * invDet;
    result.m[1] = (-m10 * b5 + m12 * b2 - m13 * b1) * invDet;
    result.m[2] = (m10 * b4 - m11 * b2 + m13 * b0) * invDet;
    result.m[3] = (-m10 * b3 + m11 * b1 - m12 * b0) * invDet;
    result.m[4] = (-m01 * b5 + m02 * b4 - m03 * b3) * invDet;
    result.m[5] = (m00 * b5 - m02 * b2 + m03 * b1) * invDet;
    result.m[6] = (-m00 * b4 + m01 * b2 - m03 * b0) * invDet;
    result.m[7] = (m00 * b3 - m01 * b1 + m02 * b0) * invDet;
    result.m[8] = (m31 * a5 - m32 * a4 + m33 * a3) * invDet;
    result.m[9] = (-m30 * a5 + m32 * a2 - m33 * a1) * invDet;
    result.m[10] = (m30 * a4 - m31 * a2 + m33 * a0) * invDet;
    result.m[11] = (-m30 * a3 + m31 * a1 - m32 * a0) * invDet;
    result.m[12] = (-m21 * a5 + m22 * a4 - m23 * a3) * invDet;
    result.m[13] = (m20 * a5 - m22 * a2 + m23 * a1) * invDet;
    result.m[14] = (-m20 * a4 + m21 * a2 - m23 * a0) * invDet;
    result.m[15] = (m20 * a3 - m21 * a1 + m22 * a0) * invDet;
    return result;
}

float Mat4::determinant() const noexcept {
    // Cofactor expansion along row 0:
    // det = sum_j (-1)^j * m[0][j] * minor(0, j).
    std::array<float, 9> minor0{};
    std::array<float, 9> minor1{};
    std::array<float, 9> minor2{};
    std::array<float, 9> minor3{};
    minor3x3(*this, 0, 0, minor0);
    minor3x3(*this, 1, 0, minor1);
    minor3x3(*this, 2, 0, minor2);
    minor3x3(*this, 3, 0, minor3);
    return (m[0] * det3(minor0)) - (m[4] * det3(minor1)) + (m[8] * det3(minor2)) -
           (m[12] * det3(minor3));
}

bool Mat4::operator==(const Mat4& other) const noexcept {
    for (int i = 0; i < 16; ++i) {
        if (std::abs(m[i] - other.m[i]) > MAT4_EPSILON) {
            return false;
        }
    }
    return true;
}

bool Mat4::operator!=(const Mat4& other) const noexcept { return !(*this == other); }

Vec4 operator*(const Mat4& matrix, const Vec4& vector) noexcept {
    return Vec4{
        (matrix.m[0] * vector.x) + (matrix.m[4] * vector.y) + (matrix.m[8] * vector.z) +
            (matrix.m[12] * vector.w),
        (matrix.m[1] * vector.x) + (matrix.m[5] * vector.y) + (matrix.m[9] * vector.z) +
            (matrix.m[13] * vector.w),
        (matrix.m[2] * vector.x) + (matrix.m[6] * vector.y) + (matrix.m[10] * vector.z) +
            (matrix.m[14] * vector.w),
        (matrix.m[3] * vector.x) + (matrix.m[7] * vector.y) + (matrix.m[11] * vector.z) +
            (matrix.m[15] * vector.w),
    };
}

Vec3 operator*(const Mat4& matrix, const Vec3& vector) noexcept {
    // Treats vector as (x, y, z, 1). Assumes an affine matrix (no perspective
    // division by the resulting w, see header); returns the transformed xyz.
    return Vec3{
        (matrix.m[0] * vector.x) + (matrix.m[4] * vector.y) + (matrix.m[8] * vector.z) +
            matrix.m[12],
        (matrix.m[1] * vector.x) + (matrix.m[5] * vector.y) + (matrix.m[9] * vector.z) +
            matrix.m[13],
        (matrix.m[2] * vector.x) + (matrix.m[6] * vector.y) + (matrix.m[10] * vector.z) +
            matrix.m[14],
    };
}

} // namespace infinity::math

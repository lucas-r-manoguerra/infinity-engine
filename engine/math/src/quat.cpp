// src/quat.cpp
#include "infinity/math/quat.h"

#include <cmath>
#include <numbers>

namespace infinity::math {

namespace {

constexpr float DEG_TO_RAD = std::numbers::pi_v<float> / 180.0f;

// Absolute epsilon used by operator== (documented in the header).
constexpr float QUAT_EPSILON = 1e-4f;

// nlerp fallback band for slerp (documented in the header): |dot| above this
// threshold means the quaternions are identical or antipodal and plain slerp
// would divide by sin(acos(dot)) ~ 0.
constexpr float SLERP_NLERP_DOT = 0.9995f;

// Linearly interpolates a and b at t, then normalizes. Used as the degenerate
// fallback of slerp so it never produces NaN.
Quat nlerp(const Quat& a, const Quat& b, float t) {
    const Quat lerp{a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t),
                    a.w + ((b.w - a.w) * t)};
    return lerp.normalized();
}

} // namespace

Quat::Quat(float x, float y, float z, float w) noexcept : x(x), y(y), z(z), w(w) {}

Quat Quat::identity() noexcept { return Quat{0.0f, 0.0f, 0.0f, 1.0f}; }

Quat Quat::fromAxisAngle(const Vec3& axis, float degrees) noexcept {
    const float lenSquared = axis.lengthSquared();
    if (lenSquared == 0.0f) {
        return identity();
    }
    const float invLen = 1.0f / std::sqrt(lenSquared);
    const float halfAngle = degrees * DEG_TO_RAD * 0.5f;
    const float s = std::sin(halfAngle);
    const float c = std::cos(halfAngle);
    return Quat{axis.x * invLen * s, axis.y * invLen * s, axis.z * invLen * s, c};
}

Quat Quat::fromYawPitchRoll(float yawDegrees, float pitchDegrees, float rollDegrees) noexcept {
    // YXZ order (rule 07): R = Ry(yaw) * Rx(pitch) * Rz(roll). operator* applies
    // the right operand first, so the product is roll, then pitch, then yaw.
    const Quat yaw = fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yawDegrees);
    const Quat pitch = fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, pitchDegrees);
    const Quat roll = fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, rollDegrees);
    return yaw * pitch * roll;
}

Quat Quat::fromMat4(const Mat4& matrix) noexcept {
    // Upper-left 3x3 read with column-major storage m[col * 4 + row].
    const float m00 = matrix.m[0];
    const float m01 = matrix.m[4];
    const float m02 = matrix.m[8];
    const float m10 = matrix.m[1];
    const float m11 = matrix.m[5];
    const float m12 = matrix.m[9];
    const float m20 = matrix.m[2];
    const float m21 = matrix.m[6];
    const float m22 = matrix.m[10];

    // Remove per-axis scale by normalizing each of the first three columns.
    const float colLenSq0 = (m00 * m00) + (m10 * m10) + (m20 * m20);
    const float colLenSq1 = (m01 * m01) + (m11 * m11) + (m21 * m21);
    const float colLenSq2 = (m02 * m02) + (m12 * m12) + (m22 * m22);
    if (colLenSq0 == 0.0f || colLenSq1 == 0.0f || colLenSq2 == 0.0f) {
        return identity();
    }
    const float inv0 = 1.0f / std::sqrt(colLenSq0);
    const float inv1 = 1.0f / std::sqrt(colLenSq1);
    const float inv2 = 1.0f / std::sqrt(colLenSq2);
    const float r00 = m00 * inv0;
    const float r10 = m10 * inv0;
    const float r20 = m20 * inv0;
    const float r01 = m01 * inv1;
    const float r11 = m11 * inv1;
    const float r21 = m21 * inv1;
    const float r02 = m02 * inv2;
    const float r12 = m12 * inv2;
    const float r22 = m22 * inv2;

    // Shepperd's method on the orthonormalized 3x3. The branch ordering
    // guarantees |cos(angle)| >= 0.5 so the chosen component never suffers
    // catastrophic cancellation; signs match the Hamilton product convention
    // used by operator* and toMat4, so fromMat4(toMat4(q)) == q.
    Quat result;
    const float trace = r00 + r11 + r22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        result.w = 0.25f * s;
        result.x = (r21 - r12) / s;
        result.y = (r02 - r20) / s;
        result.z = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        result.w = (r21 - r12) / s;
        result.x = 0.25f * s;
        result.y = (r01 + r10) / s;
        result.z = (r02 + r20) / s;
    } else if (r11 > r22) {
        const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        result.w = (r02 - r20) / s;
        result.x = (r01 + r10) / s;
        result.y = 0.25f * s;
        result.z = (r12 + r21) / s;
    } else {
        const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        result.w = (r10 - r01) / s;
        result.x = (r02 + r20) / s;
        result.y = (r12 + r21) / s;
        result.z = 0.25f * s;
    }
    return result.normalized();
}

float Quat::lengthSquared() const noexcept { return (x * x) + (y * y) + (z * z) + (w * w); }

float Quat::length() const noexcept { return std::sqrt(lengthSquared()); }

void Quat::normalize() noexcept {
    const float lenSquared = lengthSquared();
    if (lenSquared == 0.0f) {
        *this = identity();
        return;
    }
    const float invLen = 1.0f / std::sqrt(lenSquared);
    x *= invLen;
    y *= invLen;
    z *= invLen;
    w *= invLen;
}

Quat Quat::normalized() const noexcept {
    Quat result = *this;
    result.normalize();
    return result;
}

Quat Quat::conjugate() const noexcept { return Quat{-x, -y, -z, w}; }

Quat Quat::inverse() const noexcept {
    const float lenSquared = lengthSquared();
    if (lenSquared == 0.0f) {
        return identity();
    }
    const float scale = 1.0f / lenSquared;
    return Quat{-x * scale, -y * scale, -z * scale, w * scale};
}

float Quat::dot(const Quat& other) const noexcept {
    return (x * other.x) + (y * other.y) + (z * other.z) + (w * other.w);
}

Quat Quat::operator*(const Quat& other) const noexcept {
    // Hamilton product with [x, y, z, w] storage (w scalar). The sign pattern
    // matches the rotation matrices produced by toMat4.
    return Quat{(w * other.x) + (x * other.w) + (y * other.z) - (z * other.y),
                (w * other.y) - (x * other.z) + (y * other.w) + (z * other.x),
                (w * other.z) + (x * other.y) - (y * other.x) + (z * other.w),
                (w * other.w) - (x * other.x) - (y * other.y) - (z * other.z)};
}

Quat Quat::slerp(const Quat& other, float t) const noexcept {
    Quat a = normalized();
    Quat b = other.normalized();
    float dotValue = a.dot(b);
    // Shorter path (policy, see header): when the inputs are more than 90
    // degrees apart on the 4D sphere, interpolate towards -b.
    if (dotValue < 0.0f) {
        dotValue = -dotValue;
        b = Quat{-b.x, -b.y, -b.z, -b.w};
    }
    // Degenerate band (|dot| ~ 1, near-identical or near-antipodal): plain
    // slerp would divide by sin(acos(dot)) ~ 0. Normalized lerp keeps the
    // result finite and unit length.
    if (dotValue > SLERP_NLERP_DOT) {
        return nlerp(a, b, t);
    }
    const float theta = std::acos(dotValue);
    const float sinTheta = std::sin(theta);
    const float weightA = std::sin((1.0f - t) * theta) / sinTheta;
    const float weightB = std::sin(t * theta) / sinTheta;
    return Quat{(a.x * weightA) + (b.x * weightB), (a.y * weightA) + (b.y * weightB),
                (a.z * weightA) + (b.z * weightB), (a.w * weightA) + (b.w * weightB)}
        .normalized();
}

Mat4 Quat::toMat4() const noexcept {
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;
    Mat4 result = Mat4::identity();
    result.m[0] = 1.0f - 2.0f * (yy + zz);
    result.m[1] = 2.0f * (xy + wz);
    result.m[2] = 2.0f * (xz - wy);
    result.m[4] = 2.0f * (xy - wz);
    result.m[5] = 1.0f - 2.0f * (xx + zz);
    result.m[6] = 2.0f * (yz + wx);
    result.m[8] = 2.0f * (xz + wy);
    result.m[9] = 2.0f * (yz - wx);
    result.m[10] = 1.0f - 2.0f * (xx + yy);
    return result;
}

bool Quat::operator==(const Quat& other) const noexcept {
    return std::abs(x - other.x) <= QUAT_EPSILON && std::abs(y - other.y) <= QUAT_EPSILON &&
           std::abs(z - other.z) <= QUAT_EPSILON && std::abs(w - other.w) <= QUAT_EPSILON;
}

bool Quat::operator!=(const Quat& other) const noexcept { return !(*this == other); }

Vec3 operator*(const Quat& quat, const Vec3& vector) noexcept {
    // v' = q * v * q^-1 for a unit quaternion, expanded as
    // v' = v + 2*w*(u x v) + 2*(u x (u x v)) with u = (x, y, z). Requires a
    // unit quaternion (documented in the header).
    const Vec3 u{quat.x, quat.y, quat.z};
    const Vec3 t = u.cross(vector) * 2.0f;
    return vector + (t * quat.w) + u.cross(t);
}

} // namespace infinity::math

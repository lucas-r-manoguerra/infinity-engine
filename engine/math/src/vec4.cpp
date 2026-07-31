// src/vec4.cpp
#include "infinity/math/vec4.h"

#include <cmath>

namespace infinity::math {

Vec4::Vec4(float x, float y, float z, float w) noexcept : x(x), y(y), z(z), w(w) {}

Vec4 Vec4::zero() noexcept { return Vec4{0.0f, 0.0f, 0.0f, 0.0f}; }

Vec4 Vec4::one() noexcept { return Vec4{1.0f, 1.0f, 1.0f, 1.0f}; }

Vec4 Vec4::operator+(const Vec4& other) const noexcept {
    return Vec4{x + other.x, y + other.y, z + other.z, w + other.w};
}

Vec4 Vec4::operator-(const Vec4& other) const noexcept {
    return Vec4{x - other.x, y - other.y, z - other.z, w - other.w};
}

Vec4 Vec4::operator-() const noexcept { return Vec4{-x, -y, -z, -w}; }

Vec4 Vec4::operator*(float scalar) const noexcept {
    return Vec4{x * scalar, y * scalar, z * scalar, w * scalar};
}

Vec4 Vec4::operator/(float scalar) const noexcept {
    return Vec4{x / scalar, y / scalar, z / scalar, w / scalar};
}

Vec4& Vec4::operator+=(const Vec4& other) noexcept {
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;
    return *this;
}

Vec4& Vec4::operator-=(const Vec4& other) noexcept {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;
    return *this;
}

Vec4& Vec4::operator*=(float scalar) noexcept {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    w *= scalar;
    return *this;
}

Vec4& Vec4::operator/=(float scalar) noexcept {
    x /= scalar;
    y /= scalar;
    z /= scalar;
    w /= scalar;
    return *this;
}

float Vec4::dot(const Vec4& other) const noexcept {
    return (x * other.x) + (y * other.y) + (z * other.z) + (w * other.w);
}

float Vec4::lengthSquared() const noexcept { return (x * x) + (y * y) + (z * z) + (w * w); }

float Vec4::length() const noexcept { return std::sqrt(lengthSquared()); }

Vec4 Vec4::normalized() const noexcept {
    const float lenSquared = lengthSquared();
    if (lenSquared == 0.0f) {
        return zero();
    }
    const float invLen = 1.0f / std::sqrt(lenSquared);
    return Vec4{x * invLen, y * invLen, z * invLen, w * invLen};
}

Vec4 lerp(const Vec4& a, const Vec4& b, float t) noexcept { return a + (b - a) * t; }

} // namespace infinity::math

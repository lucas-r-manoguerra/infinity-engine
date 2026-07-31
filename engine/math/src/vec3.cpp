// src/vec3.cpp
#include "infinity/math/vec3.h"

#include <cmath>

namespace infinity::math {

Vec3::Vec3(float x, float y, float z) noexcept : x(x), y(y), z(z) {}

Vec3 Vec3::zero() noexcept { return Vec3{0.0f, 0.0f, 0.0f}; }

Vec3 Vec3::one() noexcept { return Vec3{1.0f, 1.0f, 1.0f}; }

Vec3 Vec3::up() noexcept { return Vec3{0.0f, 1.0f, 0.0f}; }

Vec3 Vec3::right() noexcept { return Vec3{1.0f, 0.0f, 0.0f}; }

Vec3 Vec3::forward() noexcept { return Vec3{0.0f, 0.0f, -1.0f}; }

Vec3 Vec3::operator+(const Vec3& other) const noexcept {
    return Vec3{x + other.x, y + other.y, z + other.z};
}

Vec3 Vec3::operator-(const Vec3& other) const noexcept {
    return Vec3{x - other.x, y - other.y, z - other.z};
}

Vec3 Vec3::operator-() const noexcept { return Vec3{-x, -y, -z}; }

Vec3 Vec3::operator*(float scalar) const noexcept {
    return Vec3{x * scalar, y * scalar, z * scalar};
}

Vec3 Vec3::operator/(float scalar) const noexcept {
    return Vec3{x / scalar, y / scalar, z / scalar};
}

Vec3& Vec3::operator+=(const Vec3& other) noexcept {
    x += other.x;
    y += other.y;
    z += other.z;
    return *this;
}

Vec3& Vec3::operator-=(const Vec3& other) noexcept {
    x -= other.x;
    y -= other.y;
    z -= other.z;
    return *this;
}

Vec3& Vec3::operator*=(float scalar) noexcept {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
}

Vec3& Vec3::operator/=(float scalar) noexcept {
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
}

float Vec3::dot(const Vec3& other) const noexcept {
    return (x * other.x) + (y * other.y) + (z * other.z);
}

Vec3 Vec3::cross(const Vec3& other) const noexcept {
    return Vec3{(y * other.z) - (z * other.y), (z * other.x) - (x * other.z),
                (x * other.y) - (y * other.x)};
}

float Vec3::lengthSquared() const noexcept { return (x * x) + (y * y) + (z * z); }

float Vec3::length() const noexcept { return std::sqrt(lengthSquared()); }

Vec3 Vec3::normalized() const noexcept {
    const float lenSquared = lengthSquared();
    if (lenSquared == 0.0f) {
        return zero();
    }
    const float invLen = 1.0f / std::sqrt(lenSquared);
    return Vec3{x * invLen, y * invLen, z * invLen};
}

float distance(const Vec3& a, const Vec3& b) noexcept { return (b - a).length(); }

Vec3 lerp(const Vec3& a, const Vec3& b, float t) noexcept { return a + (b - a) * t; }

} // namespace infinity::math

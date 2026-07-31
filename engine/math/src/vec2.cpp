// src/vec2.cpp
#include "infinity/math/vec2.h"

namespace infinity::math {

Vec2::Vec2(float x, float y) noexcept : x(x), y(y) {}

Vec2 Vec2::zero() noexcept { return Vec2{0.0f, 0.0f}; }

Vec2 Vec2::one() noexcept { return Vec2{1.0f, 1.0f}; }

Vec2 Vec2::operator+(const Vec2& other) const noexcept { return Vec2{x + other.x, y + other.y}; }

Vec2 Vec2::operator-(const Vec2& other) const noexcept { return Vec2{x - other.x, y - other.y}; }

Vec2 Vec2::operator-() const noexcept { return Vec2{-x, -y}; }

Vec2 Vec2::operator*(float scalar) const noexcept { return Vec2{x * scalar, y * scalar}; }

Vec2 Vec2::operator/(float scalar) const noexcept { return Vec2{x / scalar, y / scalar}; }

Vec2& Vec2::operator+=(const Vec2& other) noexcept {
    x += other.x;
    y += other.y;
    return *this;
}

Vec2& Vec2::operator-=(const Vec2& other) noexcept {
    x -= other.x;
    y -= other.y;
    return *this;
}

Vec2& Vec2::operator*=(float scalar) noexcept {
    x *= scalar;
    y *= scalar;
    return *this;
}

Vec2& Vec2::operator/=(float scalar) noexcept {
    x /= scalar;
    y /= scalar;
    return *this;
}

} // namespace infinity::math

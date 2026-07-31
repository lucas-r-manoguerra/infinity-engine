// infinity/math/vec4.h
//
// Four-component vector (F1 math core). Plain data struct with value
// semantics: operations are pure, return new vectors and never mutate their
// inputs.
#pragma once

namespace infinity::math {

// Four-dimensional vector stored as four floats, 16-byte aligned for future
// SIMD processing (F1.1).
//
// Components are public plain data (no m_ prefix, rule 02). Determinism
// policy (ADR-056): all operations use plain IEEE-754 arithmetic (no
// fast-math), so results are reproducible across platforms.
struct alignas(16) Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    // Zero-initialized vector (0, 0, 0, 0).
    Vec4() noexcept = default;

    // Component constructor.
    Vec4(float x, float y, float z, float w) noexcept;

    // Returns the zero vector (0, 0, 0, 0).
    [[nodiscard]] static Vec4 zero() noexcept;

    // Returns the vector with every component set to 1.
    [[nodiscard]] static Vec4 one() noexcept;

    // Component-wise addition.
    [[nodiscard]] Vec4 operator+(const Vec4& other) const noexcept;

    // Component-wise subtraction.
    [[nodiscard]] Vec4 operator-(const Vec4& other) const noexcept;

    // Component-wise negation.
    [[nodiscard]] Vec4 operator-() const noexcept;

    // Scales every component by scalar.
    [[nodiscard]] Vec4 operator*(float scalar) const noexcept;

    // Divides every component by scalar. IEEE-754 semantics: dividing by a
    // zero scalar yields +/-inf or NaN components, never a trap (ADR-056).
    [[nodiscard]] Vec4 operator/(float scalar) const noexcept;

    // Adds other component-wise into this vector.
    Vec4& operator+=(const Vec4& other) noexcept;

    // Subtracts other component-wise from this vector.
    Vec4& operator-=(const Vec4& other) noexcept;

    // Scales every component of this vector in place.
    Vec4& operator*=(float scalar) noexcept;

    // Divides every component of this vector in place (IEEE-754 semantics,
    // see operator/).
    Vec4& operator/=(float scalar) noexcept;

    // Dot product. Symmetric: a.dot(b) == b.dot(a).
    [[nodiscard]] float dot(const Vec4& other) const noexcept;

    // Squared length (x*x + y*y + z*z + w*w). Cheaper than length() and
    // never calls sqrt; use it for magnitude comparisons.
    [[nodiscard]] float lengthSquared() const noexcept;

    // Euclidean length.
    [[nodiscard]] float length() const noexcept;

    // Returns a unit-length vector with the same direction.
    //
    // Determinism policy (ADR-056): a zero-length vector returns the zero
    // vector (never NaN); a vector containing NaN propagates NaN to the
    // result.
    [[nodiscard]] Vec4 normalized() const noexcept;
};

// Linear interpolation between a and b at t: a + (b - a) * t. t is not
// clamped; t = 0 gives a, t = 1 gives b.
[[nodiscard]] Vec4 lerp(const Vec4& a, const Vec4& b, float t) noexcept;

} // namespace infinity::math

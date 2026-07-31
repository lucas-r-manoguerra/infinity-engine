// infinity/math/vec3.h
//
// Three-component vector (F1 math core). Follows the engine coordinate
// convention (rule 07): right-handed, +Y up, -Z forward. Plain data struct
// with value semantics: operations are pure, return new vectors and never
// mutate their inputs.
#pragma once

namespace infinity::math {

// Three-dimensional vector stored as three floats, 16-byte aligned for
// future SIMD processing (F1.1).
//
// Components are public plain data (no m_ prefix, rule 02). Determinism
// policy (ADR-056): all operations use plain IEEE-754 arithmetic (no
// fast-math), so results are reproducible across platforms.
struct alignas(16) Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    // Zero-initialized vector (0, 0, 0).
    Vec3() noexcept = default;

    // Component constructor.
    Vec3(float x, float y, float z) noexcept;

    // Returns the zero vector (0, 0, 0).
    [[nodiscard]] static Vec3 zero() noexcept;

    // Returns the vector with every component set to 1.
    [[nodiscard]] static Vec3 one() noexcept;

    // Returns the world-space up vector (0, 1, 0) per the +Y up convention.
    [[nodiscard]] static Vec3 up() noexcept;

    // Returns the world-space right vector (1, 0, 0).
    [[nodiscard]] static Vec3 right() noexcept;

    // Returns the world-space forward vector (0, 0, -1) per the -Z forward
    // convention.
    [[nodiscard]] static Vec3 forward() noexcept;

    // Component-wise addition.
    [[nodiscard]] Vec3 operator+(const Vec3& other) const noexcept;

    // Component-wise subtraction.
    [[nodiscard]] Vec3 operator-(const Vec3& other) const noexcept;

    // Component-wise negation.
    [[nodiscard]] Vec3 operator-() const noexcept;

    // Scales every component by scalar.
    [[nodiscard]] Vec3 operator*(float scalar) const noexcept;

    // Divides every component by scalar. IEEE-754 semantics: dividing by a
    // zero scalar yields +/-inf or NaN components, never a trap (ADR-056).
    [[nodiscard]] Vec3 operator/(float scalar) const noexcept;

    // Adds other component-wise into this vector.
    Vec3& operator+=(const Vec3& other) noexcept;

    // Subtracts other component-wise from this vector.
    Vec3& operator-=(const Vec3& other) noexcept;

    // Scales every component of this vector in place.
    Vec3& operator*=(float scalar) noexcept;

    // Divides every component of this vector in place (IEEE-754 semantics,
    // see operator/).
    Vec3& operator/=(float scalar) noexcept;

    // Dot product. Symmetric: a.dot(b) == b.dot(a).
    [[nodiscard]] float dot(const Vec3& other) const noexcept;

    // Cross product. The result is perpendicular to both inputs and follows
    // the right-hand rule.
    [[nodiscard]] Vec3 cross(const Vec3& other) const noexcept;

    // Squared length (x*x + y*y + z*z). Cheaper than length() and never
    // calls sqrt; use it for magnitude comparisons.
    [[nodiscard]] float lengthSquared() const noexcept;

    // Euclidean length.
    [[nodiscard]] float length() const noexcept;

    // Returns a unit-length vector with the same direction.
    //
    // Determinism policy (ADR-056): a zero-length vector returns the zero
    // vector (never NaN); a vector containing NaN propagates NaN to the
    // result.
    [[nodiscard]] Vec3 normalized() const noexcept;
};

// Euclidean distance between a and b (== (b - a).length()).
[[nodiscard]] float distance(const Vec3& a, const Vec3& b) noexcept;

// Linear interpolation between a and b at t: a + (b - a) * t. t is not
// clamped; t = 0 gives a, t = 1 gives b.
[[nodiscard]] Vec3 lerp(const Vec3& a, const Vec3& b, float t) noexcept;

} // namespace infinity::math

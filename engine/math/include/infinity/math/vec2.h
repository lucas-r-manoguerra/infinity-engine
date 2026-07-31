// infinity/math/vec2.h
//
// Two-component vector (F1 math core). Plain data struct with value
// semantics: operations are pure, return new vectors and never mutate their
// inputs (rule 02, ADR-005-style value types).
#pragma once

namespace infinity::math {

// Two-dimensional vector stored as two floats.
//
// Components are public plain data (no m_ prefix, rule 02). Determinism
// policy (ADR-056): all operations use plain IEEE-754 arithmetic; there is
// no fast-math, so results are reproducible across platforms.
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    // Zero-initialized vector (0, 0).
    Vec2() noexcept = default;

    // Component constructor.
    Vec2(float x, float y) noexcept;

    // Returns the zero vector (0, 0).
    [[nodiscard]] static Vec2 zero() noexcept;

    // Returns the vector with every component set to 1.
    [[nodiscard]] static Vec2 one() noexcept;

    // Component-wise addition.
    [[nodiscard]] Vec2 operator+(const Vec2& other) const noexcept;

    // Component-wise subtraction.
    [[nodiscard]] Vec2 operator-(const Vec2& other) const noexcept;

    // Component-wise negation.
    [[nodiscard]] Vec2 operator-() const noexcept;

    // Scales every component by scalar.
    [[nodiscard]] Vec2 operator*(float scalar) const noexcept;

    // Divides every component by scalar. IEEE-754 semantics: dividing by a
    // zero scalar yields +/-inf or NaN components, never a trap (ADR-056).
    [[nodiscard]] Vec2 operator/(float scalar) const noexcept;

    // Adds other component-wise into this vector.
    Vec2& operator+=(const Vec2& other) noexcept;

    // Subtracts other component-wise from this vector.
    Vec2& operator-=(const Vec2& other) noexcept;

    // Scales every component of this vector in place.
    Vec2& operator*=(float scalar) noexcept;

    // Divides every component of this vector in place (IEEE-754 semantics,
    // see operator/).
    Vec2& operator/=(float scalar) noexcept;
};

} // namespace infinity::math

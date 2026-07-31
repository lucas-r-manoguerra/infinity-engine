// infinity/math/mat4.h
//
// 4x4 matrix (F1 math core). Follows the engine matrix convention (rule 07):
// column-major storage, `result = a * b` means "apply b first, then a", and
// matrices transform column vectors as v' = M * v. Plain data struct with
// value semantics: operations are pure, return new matrices and never mutate
// their inputs.
#pragma once

#include "infinity/math/vec3.h"
#include "infinity/math/vec4.h"

#include <array>

namespace infinity::math {

// Four-by-four matrix stored column-major in a flat 16-float array, 16-byte
// aligned for future SIMD processing (F1.1). std::array is used so the
// storage stays a single flat block (same layout as float[16]) while keeping
// C++23 std semantics; element (row, col) lives at m[col * 4 + row]; column 3
// holds the translation for affine matrices (m[12..14]) and m[15] == 1.
// Storage is public plain data (no m_ prefix, rule 02). Determinism policy
// (ADR-056): all operations use plain IEEE-754 arithmetic (no fast-math); the
// inverse of a singular matrix returns identity() instead of NaN/Inf.
struct alignas(16) Mat4 {
    // Column-major storage: m[col * 4 + row]. Zero-initialized by default.
    std::array<float, 16> m{};

    // Returns the identity matrix.
    [[nodiscard]] static Mat4 identity() noexcept;

    // Returns a matrix that translates by the given vector.
    [[nodiscard]] static Mat4 translation(const Vec3& translation) noexcept;

    // Returns a matrix that rotates by the given angle around the +X axis.
    // Angles are in degrees (converted to radians internally, rule 07).
    // Right-handed: a positive angle maps +Y toward +Z.
    [[nodiscard]] static Mat4 rotationX(float degrees) noexcept;

    // Returns a matrix that rotates by the given angle around the +Y axis.
    // Angles are in degrees. Right-handed: a positive angle maps +Z toward +X.
    [[nodiscard]] static Mat4 rotationY(float degrees) noexcept;

    // Returns a matrix that rotates by the given angle around the +Z axis.
    // Angles are in degrees. Right-handed: a positive angle maps +X toward +Y.
    [[nodiscard]] static Mat4 rotationZ(float degrees) noexcept;

    // Returns a matrix that applies yaw (about Y), pitch (about X) and roll
    // (about Z) in the YXZ order: R = Ry(yaw) * Rx(pitch) * Rz(roll). Angles
    // are in degrees.
    [[nodiscard]] static Mat4 rotationYawPitchRoll(float yawDegrees, float pitchDegrees,
                                                   float rollDegrees) noexcept;

    // Returns a matrix that scales along each axis by the given vector.
    [[nodiscard]] static Mat4 scale(const Vec3& scale) noexcept;

    // Returns a perspective projection matrix (Vulkan/GL convention, rule 07):
    // right-handed, clip space depth 0..1 (NOT OpenGL -1..1). fovYDeg is the
    // vertical field of view in degrees, aspect is width / height, near and
    // far are the positive depth of the near and far planes. The caller must
    // pass fovYDeg in (0, 180) and 0 < near < far; invalid parameters yield
    // IEEE-754 Inf/NaN (no trap), per ADR-056.
    [[nodiscard]] static Mat4 perspective(float fovYDegrees, float aspect, float near,
                                          float far) noexcept;

    // Returns an orthographic projection matrix (Vulkan/GL convention, rule
    // 07): clip space depth 0..1 (NOT OpenGL -1..1). The caller must pass
    // left < right and bottom < top; invalid parameters yield IEEE-754
    // Inf/NaN (no trap), per ADR-056.
    [[nodiscard]] static Mat4 ortho(float left, float right, float bottom, float top, float near,
                                    float far) noexcept;

    // Matrix multiplication: `a * b` applies b first, then a (column-vector
    // convention, rule 07). Never mutates its inputs.
    [[nodiscard]] Mat4 operator*(const Mat4& other) const noexcept;

    // Returns the transposed matrix.
    [[nodiscard]] Mat4 transposed() const noexcept;

    // Returns the full 4x4 inverse (not just the affine part). Affine matrices
    // (last row exactly [0 0 0 1], e.g. TRS transforms) take a ~2x cheaper 3x3
    // fast path; perspective and general matrices use the full cofactor
    // inverse (rule 08: mat4.inverse ~18 ns on the F1 benchmark).
    //
    // Determinism policy (ADR-056): a matrix whose |determinant| is below
    // 1e-12 is treated as singular and the result is identity() — never
    // NaN/Inf. Matrices containing NaN/Inf are undefined input and the result
    // is unspecified.
    [[nodiscard]] Mat4 inverted() const noexcept;

    // Returns the determinant of the matrix.
    [[nodiscard]] float determinant() const noexcept;

    // Element-wise equality within an absolute epsilon of 1e-4.
    [[nodiscard]] bool operator==(const Mat4& other) const noexcept;

    // Negation of operator==.
    [[nodiscard]] bool operator!=(const Mat4& other) const noexcept;
};

// Transforms a column vector: v' = M * v (rule 07).
[[nodiscard]] Vec4 operator*(const Mat4& matrix, const Vec4& vector) noexcept;

// Transforms a column point: treats v as (x, y, z, 1) and returns the xyz of
// the transformed vector. Assumes an affine matrix (no perspective division
// by the resulting w); use the Vec4 overload when w != 1 matters.
[[nodiscard]] Vec3 operator*(const Mat4& matrix, const Vec3& vector) noexcept;

} // namespace infinity::math

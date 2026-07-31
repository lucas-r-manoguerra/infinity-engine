// infinity/math/quat.h
//
// Quaternion (F1 math core). Follows the engine quaternion convention (rule
// 07): internal order [x, y, z, w] with w the scalar part, 16-byte aligned
// for future SIMD processing (F1.1). Plain data struct with value semantics:
// operations are pure, return new values and never mutate their inputs
// (except the explicit in-place normalize()).
//
// Angles are degrees in the public API and radians internally; conversion
// happens exactly at the boundary (rule 07). Determinism policy (ADR-056): all
// operations use plain IEEE-754 arithmetic (no fast-math); degenerate inputs
// yield identity() or a normalized result, never NaN/Inf (see each function).
#pragma once

#include "infinity/math/mat4.h"
#include "infinity/math/vec3.h"

namespace infinity::math {

// Unit quaternion encoding a rotation. Stored as [x, y, z, w] with w the
// scalar part; the vector part (x, y, z) encodes the rotation axis.
//
// Convention: `a * b` composes quaternions by applying b first, then a
// (matches Mat4, rule 07), and `q * v` rotates a vector as v' = q * v * q^-1.
// The static factories always return unit quaternions; slerp and normalized()
// renormalize defensively. A default-constructed Quat is identity().
//
// Two quaternions q and -q encode the same rotation; operator== compares the
// raw components within an absolute epsilon and therefore treats them as
// different (use dot() or the rotation they produce to compare rotations).
struct alignas(16) Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    // Zero-initialized quaternion; defaults to identity() (w == 1).
    Quat() noexcept = default;

    // Component constructor: (x, y, z) vector part, w scalar part.
    Quat(float x, float y, float z, float w) noexcept;

    // Returns the identity quaternion (no rotation).
    [[nodiscard]] static Quat identity() noexcept;

    // Returns the quaternion that rotates by `degrees` around the given axis.
    // The axis does not need to be unit-length; a zero-length axis yields
    // identity() (never NaN, ADR-056). Angles are in degrees.
    [[nodiscard]] static Quat fromAxisAngle(const Vec3& axis, float degrees) noexcept;

    // Returns the quaternion for yaw (about +Y), pitch (about +X) and roll
    // (about +Z) in the YXZ order (rule 07): R = Ry(yaw) * Rx(pitch) *
    // Rz(roll), i.e. roll is applied first, then pitch, then yaw. Angles are
    // in degrees.
    [[nodiscard]] static Quat fromYawPitchRoll(float yawDegrees, float pitchDegrees,
                                               float rollDegrees) noexcept;

    // Extracts the rotation from a 4x4 matrix, returning a unit quaternion.
    // Per-axis scale is removed by normalizing the first three columns, so TRS
    // matrices work; translation (column 3) is ignored. Determinism policy
    // (ADR-056): a matrix with a zero-length axis column is not a rotation and
    // yields identity() (never NaN/Inf). Matrices containing NaN/Inf or shear
    // are undefined input and the result is unspecified.
    [[nodiscard]] static Quat fromMat4(const Mat4& matrix) noexcept;

    // Squared length (x*x + y*y + z*z + w*w). Cheaper than length() and never
    // calls sqrt; use it for magnitude comparisons.
    [[nodiscard]] float lengthSquared() const noexcept;

    // Euclidean length.
    [[nodiscard]] float length() const noexcept;

    // Normalizes this quaternion in place to unit length. Determinism policy
    // (ADR-056): the zero quaternion has no direction and becomes identity()
    // (never NaN/Inf).
    void normalize() noexcept;

    // Returns a unit-length quaternion with the same direction. Determinism
    // policy (ADR-056): a zero quaternion yields identity() (never NaN/Inf); a
    // quaternion containing NaN propagates NaN to the result.
    [[nodiscard]] Quat normalized() const noexcept;

    // Returns the conjugate: (-x, -y, -z, w). For a unit quaternion this is
    // the inverse rotation.
    [[nodiscard]] Quat conjugate() const noexcept;

    // Returns the multiplicative inverse. For unit quaternions the inverse is
    // exactly the conjugate (the extra length-squared scale is 1); for
    // non-unit quaternions the inverse still satisfies q * inverse(q) ==
    // identity(). Determinism policy (ADR-056): the zero quaternion has no
    // inverse and yields identity() (never NaN/Inf).
    [[nodiscard]] Quat inverse() const noexcept;

    // 4D dot product. Symmetric: a.dot(b) == b.dot(a). For two unit
    // quaternions, dot == cos(angle/2) of the rotation between them; a
    // negative dot means the two quaternions are more than 90 degrees apart on
    // the 4D sphere.
    [[nodiscard]] float dot(const Quat& other) const noexcept;

    // Spherical interpolation between this and `other` at t (t = 0 gives this,
    // t = 1 gives other; t is not clamped).
    //
    // Determinism policy (ADR-056), documented here because it is a contract:
    // - Inputs are normalized before AND after interpolating, so the result is
    //   always unit length even for non-unit inputs.
    // - When the inputs are more than 90 degrees apart (dot < 0) the end
    //   quaternion is negated so interpolation takes the shorter arc around
    //   the sphere.
    // - When the inputs are identical or antipodal (|dot| > 0.9995, i.e. the
    //   rotation between them is near 0 or 180 degrees) plain slerp would
    //   divide by sin(acos(dot)) == 0 and produce NaN; those cases fall back
    //   to normalized linear interpolation (nlerp), so the result is always
    //   finite and unit length. Use slerp for the shortest, constant-speed
    //   path between rotations; the nlerp fallback only engages in the
    //   degenerate band where slerp is numerically undefined.
    [[nodiscard]] Quat slerp(const Quat& other, float t) const noexcept;

    // Returns the 3x3 rotation of this quaternion embedded in an identity 4x4
    // matrix (column-major, Mat4 convention). Matches the matrices produced by
    // Mat4::rotationX/Y/Z and Mat4::rotationYawPitchRoll for the same angles.
    [[nodiscard]] Mat4 toMat4() const noexcept;

    // Quaternion composition: `this * other` applies other first, then this
    // (Hamilton product, [x, y, z, w] storage). Never mutates its inputs.
    [[nodiscard]] Quat operator*(const Quat& other) const noexcept;

    // Component-wise equality within an absolute epsilon of 1e-4.
    [[nodiscard]] bool operator==(const Quat& other) const noexcept;

    // Negation of operator==.
    [[nodiscard]] bool operator!=(const Quat& other) const noexcept;
};

// Rotates a vector: v' = q * v * q^-1. Assumes `quat` is a unit quaternion
// (as produced by the factories and slerp; call normalized() if unsure).
[[nodiscard]] Vec3 operator*(const Quat& quat, const Vec3& vector) noexcept;

} // namespace infinity::math

// infinity/math/transform.h
//
// Transform (F1 math core): a full TRS (translation-rotation-scale) transform
// unit, the engine's shared representation for object placement and
// hierarchy. Follows rule 07: right-handed, +Y up, -Z forward, column-major
// matrices, angles in degrees at the public boundary, SRT order
// (M = T * R * S), and quaternion [x, y, z, w] storage. Plain data struct
// with value semantics: operations are pure, return new values and never
// mutate their inputs.
#pragma once

#include "infinity/math/mat4.h"
#include "infinity/math/quat.h"
#include "infinity/math/vec3.h"

namespace infinity::math {

// A transform unit: position (translation), rotation (quaternion) and scale.
//
// Defaults match the identity transform: position zero, rotation identity,
// scale one. The derived matrix is M = T * R * S (scale first, then rotation,
// then translation, rule 07), equivalent to
// Mat4::translation(position) * rotation.toMat4() * Mat4::scale(scale).
//
// Child/parent composition: `child * parent` applies the child's local
// transform first and the parent's after, so the composed matrix is
// matrix(parent) * matrix(child). With non-uniform scale this TRS composition
// is exact only when rotation and scale commute (e.g. uniform parent scale);
// otherwise it introduces no shear but differs from the raw matrix product
// (standard engine convention, same as Unity/Godot-style TRS composition).
// Public members are plain data (no m_ prefix, rule 02).
struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};

    // Identity transform: no translation, no rotation, unit scale.
    Transform() noexcept = default;

    // Full TRS constructor.
    Transform(const Vec3& position, const Quat& rotation, const Vec3& scale) noexcept;

    // Returns the identity transform (position zero, rotation identity,
    // scale one).
    [[nodiscard]] static Transform identity() noexcept;

    // Returns the 4x4 matrix of this transform, M = T * R * S (SRT order,
    // rule 07).
    [[nodiscard]] Mat4 matrix() const noexcept;

    // Transforms a point through the full TRS: p' = T * R * S * p.
    [[nodiscard]] Vec3 transformPoint(const Vec3& point) const noexcept;

    // Transforms a direction (rotation only): d' = R * d. Translation and
    // scale are ignored, so the result keeps its length.
    [[nodiscard]] Vec3 transformDirection(const Vec3& direction) const noexcept;
};

// Child/parent composition: applies the child's local transform first, then
// the parent's. The combined rotation is parent.rotation * child.rotation
// (child first, then parent), the combined scale is the component-wise
// product, and the combined position is
// parent.position + parent.rotation * (parent.scale * child.position).
[[nodiscard]] Transform operator*(const Transform& child, const Transform& parent) noexcept;

} // namespace infinity::math

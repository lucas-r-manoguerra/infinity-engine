// src/transform.cpp
#include "infinity/math/transform.h"

namespace infinity::math {

namespace {

// Component-wise multiplication of two vectors (there is no Vec3 * Vec3
// operator in the math core; composition and point scaling need per-axis
// products).
[[nodiscard]] Vec3 multiplyComponentwise(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x * b.x, a.y * b.y, a.z * b.z};
}

} // namespace

Transform::Transform(const Vec3& position, const Quat& rotation, const Vec3& scale) noexcept
    : position(position), rotation(rotation), scale(scale) {}

Transform Transform::identity() noexcept { return Transform{}; }

Mat4 Transform::matrix() const noexcept {
    // SRT order (rule 07): M = T * R * S. operator* applies the right operand
    // first, so the point is scaled, then rotated, then translated.
    return Mat4::translation(position) * rotation.toMat4() * Mat4::scale(scale);
}

Vec3 Transform::transformPoint(const Vec3& point) const noexcept {
    return position + (rotation * multiplyComponentwise(scale, point));
}

Vec3 Transform::transformDirection(const Vec3& direction) const noexcept {
    // Rotation only: translation and scale are ignored (rule 07, SRT).
    return rotation * direction;
}

Transform operator*(const Transform& child, const Transform& parent) noexcept {
    // Applies the child's local transform first, then the parent's (rule 07:
    // `a * b` applies b first). The combined transform keeps the TRS form:
    //   rotation = parent.rotation * child.rotation
    //   scale    = parent.scale * child.scale (component-wise)
    //   position = parent.position
    //            + parent.rotation * (parent.scale * child.position)
    Transform result;
    result.rotation = parent.rotation * child.rotation;
    result.scale = multiplyComponentwise(parent.scale, child.scale);
    result.position =
        parent.position + (parent.rotation * multiplyComponentwise(parent.scale, child.position));
    return result;
}

} // namespace infinity::math

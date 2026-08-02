// infinity/renderer/camera.h
#pragma once

#include "infinity/math/mat4.h"
#include "infinity/math/quat.h"
#include "infinity/math/vec2.h"
#include "infinity/math/vec3.h"
#include "infinity/renderer/error.h"

#include <cstdint>
#include <optional>

namespace infinity::renderer {

/// A first-class camera as plain data (ADR-051, ADR-038): the pose and
/// projection parameters live in the world and serialize/replay like any other
/// data. Building matrices from it is a pure function — the Camera never
/// accumulates state.
///
/// Coordinate contract (rule 07): right-handed, +Y up, -Z forward. The camera
/// looks along its forward axis and never rolls (Y-up world, roll left as 0).
/// The field of view is expressed in degrees in the public API; the matrix
/// builder converts to radians internally.
struct Camera {
    /// World-space position of the eye (unit: meters).
    math::Vec3 position = math::Vec3::zero();
    /// Orientation of the camera in world space.
    math::Quat rotation = math::Quat::identity();
    /// Vertical field of view in degrees, in the open interval (0, 180).
    float fovYDegrees = 60.0f;
    /// Width / height of the target surface. Must be > 0.
    float aspect = 1.0f;
    /// Distance to the near plane. Must be > 0.
    float near = 0.1f;
    /// Distance to the far plane. Must be > `near`.
    float far = 1000.0f;
};

/// Builds the column-major view-projection matrix for `camera` (rule 07):
/// `result = projection * view`, so `result * v` transforms a world-space
/// column vector into clip space. The view is derived SRT-style:
/// `view = inverse(translation(position) * rotation.toMat4())`, yielding a
/// camera that looks down its local -Z with +Y up.
///
/// @returns the combined matrix, or `RenderError::INVALID_ARGUMENT` when the
/// camera is degenerate: fov outside (0, 180), aspect <= 0, near <= 0, or
/// far <= near.
[[nodiscard]] Expected<math::Mat4> buildViewProjection(const Camera& camera) noexcept;

/// Projects a world-space point through `viewProjection` into the draw-list
/// pixel space (rule 07 / draw_list.h): top-left origin, +Y down, pixels
/// span [0, width) x [0, height). The projected point is clamped by the
/// standard NDC window transform.
///
/// @returns the pixel position, or `std::nullopt` when the point is behind the
/// camera or outside the frustum (w <= 0 or x/y outside [-1, 1]), or when the
/// target size is degenerate (width == 0 || height == 0).
///
/// @note Depth (NDC z) and near-plane clipping are not resolved here: deciding
/// whether a projected vertex is visible belongs to rasterization/triangle
/// clipping, which is future work.
[[nodiscard]] std::optional<math::Vec2> projectWorldToScreen(const math::Vec3& worldPoint,
                                                             const math::Mat4& viewProjection,
                                                             std::uint32_t width,
                                                             std::uint32_t height) noexcept;

} // namespace infinity::renderer

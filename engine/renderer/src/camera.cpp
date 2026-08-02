// infinity/renderer/camera.cpp
#include "infinity/renderer/camera.h"

namespace infinity::renderer {

namespace {

bool isValidCamera(const Camera& camera) noexcept {
    return camera.fovYDegrees > 0.0f && camera.fovYDegrees < 180.0f && camera.aspect > 0.0f &&
           camera.near > 0.0f && camera.far > camera.near;
}

} // namespace

Expected<math::Mat4> buildViewProjection(const Camera& camera) noexcept {
    if (!isValidCamera(camera)) {
        return std::unexpected(RenderError::INVALID_ARGUMENT);
    }

    // Right-handed projection (rule 07): fov is vertical, the horizontal
    // half-frustum is scaled by aspect, and depth maps to [0, 1] clip space.
    // Mat4::perspective matches this convention exactly.
    const math::Mat4 projection =
        math::Mat4::perspective(camera.fovYDegrees, camera.aspect, camera.near, camera.far);

    // The view is the inverse of the camera-to-world transform (SRT order,
    // rule 07): inverse(translation(position) * rotation) = R^T * T(-position).
    // Quat::toMat4 is orthonormal, so its inverse is its transpose (upper-left
    // block); the translation is then applied in view space: -R^T * position.
    const math::Mat4 rotationMatrix = camera.rotation.toMat4();
    math::Mat4 view;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            view.m[(col * 4) + row] = rotationMatrix.m[(row * 4) + col];
        }
    }
    view.m[15] = 1.0f;

    const float px = camera.position.x;
    const float py = camera.position.y;
    const float pz = camera.position.z;
    view.m[12] = -((view.m[0] * px) + (view.m[1] * py) + (view.m[2] * pz));
    view.m[13] = -((view.m[4] * px) + (view.m[5] * py) + (view.m[6] * pz));
    view.m[14] = -((view.m[8] * px) + (view.m[9] * py) + (view.m[10] * pz));

    return projection * view;
}

std::optional<math::Vec2> projectWorldToScreen(const math::Vec3& worldPoint,
                                               const math::Mat4& viewProjection,
                                               std::uint32_t width, std::uint32_t height) noexcept {
    if (width == 0 || height == 0) {
        return std::nullopt;
    }

    // Transform the world point into clip space as a column vector (rule 07).
    const float clipX = (viewProjection.m[0] * worldPoint.x) +
                        (viewProjection.m[4] * worldPoint.y) +
                        (viewProjection.m[8] * worldPoint.z) + viewProjection.m[12];
    const float clipY = (viewProjection.m[1] * worldPoint.x) +
                        (viewProjection.m[5] * worldPoint.y) +
                        (viewProjection.m[9] * worldPoint.z) + viewProjection.m[13];
    const float clipW = (viewProjection.m[3] * worldPoint.x) +
                        (viewProjection.m[7] * worldPoint.y) +
                        (viewProjection.m[11] * worldPoint.z) + viewProjection.m[15];

    // A point at or behind the camera plane has w <= 0 and must never be
    // projected (the division below would mirror it into the view).
    if (clipW <= 0.0f) {
        return std::nullopt;
    }

    // Perspective divide into normalized device coordinates.
    const float ndcX = clipX / clipW;
    const float ndcY = clipY / clipW;

    // Points outside the [-1, 1] frustum are not on the target surface.
    if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) {
        return std::nullopt;
    }

    // Window transform for the draw-list pixel space (top-left origin, +y
    // down, rule 07 / draw_list.h): NDC is centered with +y up.
    return math::Vec2{(ndcX + 1.0f) * 0.5f * static_cast<float>(width),
                      (1.0f - ndcY) * 0.5f * static_cast<float>(height)};
}

} // namespace infinity::renderer

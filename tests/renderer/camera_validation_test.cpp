// tests/renderer/camera_validation_test.cpp
//
// Contract tests for camera and projection validation (F4.10, ADR-051, rule
// 04): buildViewProjection rejects cameras with invalid fov, aspect or near/far
// planes, and projectWorldToScreen rejects points behind the camera, outside
// the frustum, and degenerate target sizes — always as explicit errors, never
// UB. Projection math cases live in camera_projection_test.cpp (rule 01: One
// File = One Task).
#include "infinity/math/mat4.h"
#include "infinity/math/vec2.h"
#include "infinity/math/vec3.h"
#include "infinity/renderer/camera.h"
#include "infinity/renderer/error.h"

#include <cstdint>
#include <limits>

#include <doctest/doctest.h>

namespace {

using infinity::math::Mat4;
using infinity::math::Vec2;
using infinity::math::Vec3;
using infinity::renderer::buildViewProjection;
using infinity::renderer::Camera;
using infinity::renderer::projectWorldToScreen;
using infinity::renderer::RenderError;

constexpr std::uint32_t WIDTH = 800;
constexpr std::uint32_t HEIGHT = 600;

// Isolated from CHECK so doctest never stringifies a RenderError (ADL).
bool isInvalidArgument(const infinity::renderer::Expected<Mat4>& result) {
    return !result.has_value() && result.error() == RenderError::INVALID_ARGUMENT;
}

// Camera looking straight down -Z from the origin with a 90-degree fov and
// square aspect, so the frustum is symmetric and the half-frustum tangent is 1.
Camera originCamera() {
    return Camera{.fovYDegrees = 90.0f, .aspect = 1.0f, .near = 0.1f, .far = 1000.0f};
}

// Projects `point` through the camera and checks it is rejected.
void checkNoProjection(const Camera& camera, const Vec3& point) {
    const infinity::renderer::Expected<Mat4> viewProjection = buildViewProjection(camera);
    CHECK(viewProjection.has_value());
    if (!viewProjection.has_value()) {
        return;
    }
    const std::optional<Vec2> projected =
        projectWorldToScreen(point, *viewProjection, WIDTH, HEIGHT);
    CHECK_FALSE(projected.has_value());
}

} // namespace

TEST_CASE("buildViewProjection rejects an invalid fov") {
    // Each case starts from a valid camera and breaks exactly one parameter.
    {
        Camera camera = originCamera();
        camera.fovYDegrees = 0.0f;
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
    {
        Camera camera = originCamera();
        camera.fovYDegrees = 180.0f;
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
    {
        Camera camera = originCamera();
        camera.fovYDegrees = -60.0f;
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
    {
        Camera camera = originCamera();
        camera.fovYDegrees = std::numeric_limits<float>::quiet_NaN();
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
}

TEST_CASE("buildViewProjection rejects an invalid aspect") {
    // Each case starts from a valid camera and breaks exactly one parameter.
    {
        Camera camera = originCamera();
        camera.aspect = 0.0f;
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
    {
        Camera camera = originCamera();
        camera.aspect = -1.0f;
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
}

TEST_CASE("buildViewProjection rejects invalid near/far planes") {
    // Each case starts from a valid camera and breaks exactly one parameter.
    {
        Camera camera = originCamera();
        camera.near = 0.0f;
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
    {
        Camera camera = originCamera();
        camera.near = -0.5f;
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
    {
        Camera camera = originCamera();
        camera.far = camera.near;
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
    {
        Camera camera = originCamera();
        camera.far = camera.near * 0.5f;
        CHECK(isInvalidArgument(buildViewProjection(camera)));
    }
}

TEST_CASE("points behind the camera return nullopt") {
    const Camera camera = originCamera();
    checkNoProjection(camera, Vec3{0.0f, 0.0f, 5.0f});
    checkNoProjection(camera, Vec3{0.0f, 0.0f, 0.0f}); // on the camera plane (w == 0)
    checkNoProjection(camera, Vec3{3.0f, -2.0f, 1.0f});
}

TEST_CASE("points outside the frustum return nullopt") {
    const Camera camera = originCamera();
    checkNoProjection(camera, Vec3{100.0f, 0.0f, -5.0f});
    checkNoProjection(camera, Vec3{-100.0f, 0.0f, -5.0f});
    checkNoProjection(camera, Vec3{0.0f, 100.0f, -5.0f});
    checkNoProjection(camera, Vec3{0.0f, -100.0f, -5.0f});
    checkNoProjection(camera, Vec3{6.0f, 0.0f, -5.0f}); // just past the right edge
}

TEST_CASE("a degenerate target size returns nullopt") {
    const Camera camera = originCamera();
    const infinity::renderer::Expected<Mat4> viewProjection = buildViewProjection(camera);
    CHECK(viewProjection.has_value());
    if (!viewProjection.has_value()) {
        return;
    }
    const Vec3 point{0.0f, 0.0f, -5.0f};
    CHECK_FALSE(projectWorldToScreen(point, *viewProjection, 0, 0).has_value());
    CHECK_FALSE(projectWorldToScreen(point, *viewProjection, 0, HEIGHT).has_value());
    CHECK_FALSE(projectWorldToScreen(point, *viewProjection, WIDTH, 0).has_value());
}

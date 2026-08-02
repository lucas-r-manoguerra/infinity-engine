// tests/renderer/camera_test.cpp
//
// Contract tests for first-class cameras (F4.10, ADR-051): Camera is a plain,
// serializable data struct (ADR-038, rule 11) with documented defaults;
// buildViewProjection validates the camera (rule 04) and returns the
// column-major view-projection matrix; projectWorldToScreen maps NDC to the
// draw-list pixel space (top-left origin, +y down) and rejects points behind
// the camera, outside the frustum, and degenerate targets. Projection math is
// deterministic (rule 11): the same camera yields the same matrix, and the
// degree-based fov maps the true frustum edge to the target edge.
#include "infinity/math/mat4.h"
#include "infinity/math/quat.h"
#include "infinity/math/vec2.h"
#include "infinity/math/vec3.h"
#include "infinity/renderer/camera.h"
#include "infinity/renderer/error.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

#include <doctest/doctest.h>

namespace {

using infinity::math::Mat4;
using infinity::math::Quat;
using infinity::math::Vec2;
using infinity::math::Vec3;
using infinity::renderer::buildViewProjection;
using infinity::renderer::Camera;
using infinity::renderer::projectWorldToScreen;
using infinity::renderer::RenderError;

constexpr float EPSILON = 1e-4f;
constexpr std::uint32_t WIDTH = 800;
constexpr std::uint32_t HEIGHT = 600;

// Approx with a tolerance suited to float projection math (relative 1e-4).
doctest::Approx near(float value) { return doctest::Approx(value).epsilon(1e-4); }

// Element-wise comparison of two matrices within the absolute 1e-4 tolerance.
bool matricesNear(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (!(std::abs(a.m[i] - b.m[i]) <= EPSILON)) {
            return false;
        }
    }
    return true;
}

// Bit-exact comparison of two matrices (rule 11: same input, same output).
bool matricesBitEqual(const Mat4& a, const Mat4& b) {
    for (int i = 0; i < 16; ++i) {
        if (a.m[i] != b.m[i]) {
            return false;
        }
    }
    return true;
}

// Isolated from CHECK so doctest never stringifies a RenderError (ADL).
bool isInvalidArgument(const infinity::renderer::Expected<Mat4>& result) {
    return !result.has_value() && result.error() == RenderError::INVALID_ARGUMENT;
}

// Camera looking straight down -Z from the origin with a 90-degree fov and
// square aspect, so the frustum is symmetric and the half-frustum tangent is 1.
Camera originCamera() {
    return Camera{.fovYDegrees = 90.0f, .aspect = 1.0f, .near = 0.1f, .far = 1000.0f};
}

// Projects `point` through the camera and checks it lands at the expected
// pixel (draw-list space: top-left origin, +y down).
void checkProjection(const Camera& camera, const Vec3& point, float expectedX, float expectedY) {
    const infinity::renderer::Expected<Mat4> viewProjection = buildViewProjection(camera);
    CHECK(viewProjection.has_value());
    if (!viewProjection.has_value()) {
        return;
    }
    const std::optional<Vec2> projected =
        projectWorldToScreen(point, *viewProjection, WIDTH, HEIGHT);
    CHECK(projected.has_value());
    if (!projected.has_value()) {
        return;
    }
    CHECK(projected->x == near(expectedX));
    CHECK(projected->y == near(expectedY));
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

TEST_CASE("Camera defaults describe a valid identity-pose camera") {
    const Camera camera{};
    CHECK(camera.position.x == 0.0f);
    CHECK(camera.position.y == 0.0f);
    CHECK(camera.position.z == 0.0f);
    CHECK(camera.rotation == Quat::identity());
    CHECK(camera.fovYDegrees == 60.0f);
    CHECK(camera.aspect == 1.0f);
    CHECK(camera.near == 0.1f);
    CHECK(camera.far == 1000.0f);
    CHECK(buildViewProjection(camera).has_value());
}

TEST_CASE("an identity-pose camera produces exactly the raw projection") {
    const Camera camera = originCamera();
    const infinity::renderer::Expected<Mat4> viewProjection = buildViewProjection(camera);
    CHECK(viewProjection.has_value());
    if (viewProjection.has_value()) {
        CHECK(matricesNear(*viewProjection, Mat4::perspective(90.0f, 1.0f, 0.1f, 1000.0f)));
    }
}

TEST_CASE("a point straight ahead of an identity camera projects to the target center") {
    const Camera camera = originCamera();
    checkProjection(camera, Vec3{0.0f, 0.0f, -5.0f}, 400.0f, 300.0f);
    checkProjection(camera, Vec3{0.0f, 0.0f, -2.0f}, 400.0f, 300.0f);
}

TEST_CASE("points at the frustum edge land exactly on the target edge (degrees, not radians)") {
    // For fov 90 and aspect 1 the half-frustum tangent is tan(45 deg) == 1, so
    // the frustum half-width at distance 5 is 5. The edge point is computed
    // from the engine's own degree conversion to pin the contract: if fov were
    // read as radians, that point would not land at the pixel edge.
    const float halfFovRadians = 90.0f * std::numbers::pi_v<float> / 180.0f * 0.5f;
    const float halfFrustumAtDistance = std::tan(halfFovRadians) * 5.0f;
    const Camera camera = originCamera();
    checkProjection(camera, Vec3{-halfFrustumAtDistance, 0.0f, -5.0f}, 0.0f, 300.0f);
    checkProjection(camera, Vec3{halfFrustumAtDistance, 0.0f, -5.0f}, 800.0f, 300.0f);
}

TEST_CASE("projection uses a top-left origin with +y downward") {
    const Camera camera = originCamera();
    // World +Y is up; the frustum half-height at distance 5 is 5 (fov 90,
    // aspect 1), so a point 5 units above the view axis maps to the top edge
    // (smallest screen y) and 5 units below maps to the bottom edge.
    checkProjection(camera, Vec3{0.0f, 5.0f, -5.0f}, 400.0f, 0.0f);
    checkProjection(camera, Vec3{0.0f, -5.0f, -5.0f}, 400.0f, 600.0f);
}

TEST_CASE("camera translation shifts the world origin of the view") {
    Camera camera = originCamera();
    camera.position = Vec3{10.0f, 0.0f, 0.0f};
    // The camera now sits at x = 10 looking toward -Z: the point 5 units ahead
    // of it (world x = 10) is centered, and a point at its left frustum edge is
    // world x = 5 for this fov (the old origin is not even visible).
    checkProjection(camera, Vec3{10.0f, 0.0f, -5.0f}, 400.0f, 300.0f);
    checkProjection(camera, Vec3{5.0f, 0.0f, -5.0f}, 0.0f, 300.0f);
}

TEST_CASE("camera yaw rotation turns the view and changes what appears in front") {
    // Rule 07, right-handed +Y up: a positive yaw about +Y rotates the forward
    // axis from -Z toward -X, so a camera yawed +90 degrees looks toward -X and
    // yawed -90 degrees looks toward +X. A point that was to the right of the
    // identity camera (+X world) appears straight ahead after a -90 yaw, and
    // the same point is behind a +90 yaw (direction contract).
    Camera yawRight = originCamera();
    yawRight.rotation = Quat::fromYawPitchRoll(-90.0f, 0.0f, 0.0f);
    checkProjection(yawRight, Vec3{5.0f, 0.0f, 0.0f}, 400.0f, 300.0f);
    checkNoProjection(yawRight, Vec3{-5.0f, 0.0f, 0.0f});

    Camera yawLeft = originCamera();
    yawLeft.rotation = Quat::fromYawPitchRoll(90.0f, 0.0f, 0.0f);
    checkProjection(yawLeft, Vec3{-5.0f, 0.0f, 0.0f}, 400.0f, 300.0f);
    checkNoProjection(yawLeft, Vec3{5.0f, 0.0f, 0.0f});
}

TEST_CASE("aspect scales the horizontal field of view") {
    Camera camera = originCamera();
    camera.aspect = 2.0f;
    // With aspect 2 the horizontal half-frustum is twice the vertical one: the
    // left and right edges of the view at distance 5 sit at world x = -10/10.
    checkProjection(camera, Vec3{-10.0f, 0.0f, -5.0f}, 0.0f, 300.0f);
    checkProjection(camera, Vec3{10.0f, 0.0f, -5.0f}, 800.0f, 300.0f);
    checkProjection(camera, Vec3{0.0f, 0.0f, -5.0f}, 400.0f, 300.0f);
}

TEST_CASE("identical cameras produce bit-identical view-projections (rule 11)") {
    Camera camera = originCamera();
    camera.position = Vec3{-2.0f, 3.0f, 4.0f};
    camera.rotation = Quat::fromYawPitchRoll(35.0f, -10.0f, 5.0f);
    camera.fovYDegrees = 45.0f;
    camera.aspect = 16.0f / 9.0f;
    const infinity::renderer::Expected<Mat4> first = buildViewProjection(camera);
    const infinity::renderer::Expected<Mat4> second = buildViewProjection(camera);
    CHECK(first.has_value());
    CHECK(second.has_value());
    if (first.has_value() && second.has_value()) {
        CHECK(matricesBitEqual(*first, *second));
        // Two independent projections of the same visible point also agree
        // bit-exactly. The point sits 5 meters ahead of the rotated camera, so
        // it is guaranteed to be inside the frustum.
        const Mat4 rotationMatrix = camera.rotation.toMat4();
        const Vec3 forward = rotationMatrix * Vec3{0.0f, 0.0f, -1.0f};
        const Vec3 point = camera.position + forward * 5.0f;
        const std::optional<Vec2> a = projectWorldToScreen(point, *first, WIDTH, HEIGHT);
        const std::optional<Vec2> b = projectWorldToScreen(point, *second, WIDTH, HEIGHT);
        CHECK(a.has_value());
        CHECK(b.has_value());
        if (a.has_value() && b.has_value()) {
            CHECK(a->x == b->x);
            CHECK(a->y == b->y);
        }
    }
}

TEST_CASE("buildViewProjection rejects cameras with invalid parameters") {
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

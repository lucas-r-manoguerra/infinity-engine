const std = @import("std");
const testing = std.testing;
const Vec3 = @import("../../math/vec3.zig").Vec3;
const Mat4 = @import("../../math/mat4.zig").Mat4;

// Test face normal computation used in Lambertian lighting.
// For a simple front-facing triangle on the XY plane, normal should be +Z.
test "face normal from triangle vertices is +Z" {
    const p0 = Vec3.init(0, 0, 0);
    const p1 = Vec3.init(1, 0, 0);
    const p2 = Vec3.init(0, 1, 0);
    const edge1 = Vec3.sub(p1, p0);
    const edge2 = Vec3.sub(p2, p0);
    const normal = Vec3.normalize(Vec3.cross(edge1, edge2));
    try testing.expectApproxEqAbs(@as(f32, 0), normal.x, 0.001);
    try testing.expectApproxEqAbs(@as(f32, 0), normal.y, 0.001);
    try testing.expectApproxEqAbs(@as(f32, 1), normal.z, 0.001);
}

// A rotated triangle should produce a rotated normal.
test "face normal rotates with triangle" {
    // Rotated 90° around X: the YZ-plane triangle
    const p0 = Vec3.init(0, 0, 0);
    const p1 = Vec3.init(0, 1, 0);
    const p2 = Vec3.init(0, 0, 1);
    const edge1 = Vec3.sub(p1, p0);
    const edge2 = Vec3.sub(p2, p0);
    const normal = Vec3.normalize(Vec3.cross(edge1, edge2));
    try testing.expectApproxEqAbs(@as(f32, 1), normal.x, 0.001);
    try testing.expectApproxEqAbs(@as(f32, 0), normal.y, 0.001);
    try testing.expectApproxEqAbs(@as(f32, 0), normal.z, 0.001);
}

// Test transformDirection preserves unit length.
test "transformDirection preserves normal length" {
    const model = Mat4.mul(
        Mat4.translate(Vec3.init(1, 2, 3)),
        Mat4.rotateY(1.5),
    );
    const normal = Vec3.normalize(Vec3.init(1, 2, 1));
    const transformed = Mat4.transformDirection(model, normal);
    const len = Vec3.length(transformed);
    // transformDirection uses 3×3 part (rotation only, no translation)
    // Since rotateY keeps unit vectors unit:
    try testing.expectApproxEqAbs(@as(f32, 1.0), len, 0.001);
}

// Light direction should be unit length.
test "LIGHT_DIR is unit length" {
    const ld = Vec3.normalize(Vec3.init(1.0, 2.0, 1.0));
    const len = Vec3.length(ld);
    try testing.expectApproxEqAbs(@as(f32, 1.0), len, 0.001);
}

// The Lambertian factor for a fully lit normal should be 1.0.
test "lighting factor at full intensity" {
    const light = Vec3.normalize(Vec3.init(1.0, 2.0, 1.0));
    const normal = Vec3.init(0.408, 0.816, 0.408); // same direction as light
    const factor = 0.3 + 0.7 * @max(Vec3.dot(normal, light), 0.0);
    try testing.expectApproxEqAbs(@as(f32, 1.0), factor, 0.01);
}

// The Lambertian factor for an unlit normal should be 0.3 (ambient only).
test "lighting factor at zero intensity" {
    const light = Vec3.normalize(Vec3.init(1.0, 2.0, 1.0));
    const normal = Vec3.init(-0.408, -0.816, -0.408); // opposite direction
    const factor = 0.3 + 0.7 * @max(Vec3.dot(normal, light), 0.0);
    try testing.expectApproxEqAbs(@as(f32, 0.3), factor, 0.01);
}

//! 3D Transform: position, rotation, scale.
//!
//! Combines Vec3 and Quat into a complete spatial description.

const std = @import("std");
const Vec3 = @import("vec3.zig").Vec3;
const Mat4 = @import("mat4.zig").Mat4;
const Quat = @import("quat.zig").Quat;

/// 3D Transform with TRS (Translation-Rotation-Scale).
/// Efficient local-to-world and world-to-local conversions.
pub const Transform = struct {
    position: Vec3 = .zero,
    rotation: Quat = .identity,
    scale: Vec3 = .one,

    /// Create a transform from position only.
    pub fn fromPosition(pos: Vec3) Transform {
        return .{ .position = pos };
    }

    /// Build the local-to-world matrix from this transform.
    pub fn toMatrix(self: Transform) Mat4 {
        const t = Mat4.translate(self.position);
        const r = Mat4.fromQuat(self.rotation);
        const s = Mat4.scale(self.scale);
        return Mat4.mul(Mat4.mul(t, r), s);
    }

    /// Translate relative to local axes.
    pub fn translate(self: *Transform, delta: Vec3) void {
        self.position = Vec3.add(self.position, delta);
    }

    /// Rotate by a quaternion (local space).
    pub fn rotate(self: *Transform, q: Quat) void {
        self.rotation = Quat.mul(self.rotation, q);
    }

    /// Forward direction vector (local -Z).
    pub fn forward(self: Transform) Vec3 {
        return Quat.rotate(self.rotation, Vec3.forward);
    }

    /// Right direction vector (local +X).
    pub fn right(self: Transform) Vec3 {
        return Quat.rotate(self.rotation, Vec3.right);
    }

    /// Up direction vector (local +Y).
    pub fn up(self: Transform) Vec3 {
        return Quat.rotate(self.rotation, Vec3.up);
    }

    /// Look at a target position.
    pub fn lookAt(self: *Transform, target: Vec3, world_up: Vec3) void {
        _ = world_up;
        const direction = Vec3.normalize(Vec3.sub(target, self.position));
        // Simple look-at: align -Z with direction
        const dot = Vec3.dot(Vec3.forward, direction);
        if (dot > 0.9999) return;
        if (dot < -0.9999) {
            self.rotation = Quat.fromAxisAngle(Vec3.up, std.math.pi);
            return;
        }
        const axis = Vec3.normalize(Vec3.cross(Vec3.forward, direction));
        const angle = std.math.acos(dot);
        self.rotation = Quat.fromAxisAngle(axis, angle);
    }
};

test "transform identity matrix" {
    const t = Transform{};
    const m = t.toMatrix();
    try std.testing.expectEqual(@as(f32, 1), m.data[0]);
    try std.testing.expectEqual(@as(f32, 1), m.data[5]);
    try std.testing.expectEqual(@as(f32, 1), m.data[10]);
    try std.testing.expectEqual(@as(f32, 1), m.data[15]);
}

test "transform translation" {
    var t = Transform.fromPosition(Vec3.init(10, 20, 30));
    const p = Vec3.init(0, 0, 0);
    const m = t.toMatrix();
    const result = Mat4.transformPoint(m, p);
    try std.testing.expectEqual(@as(f32, 10), result.x);
    try std.testing.expectEqual(@as(f32, 20), result.y);
    try std.testing.expectEqual(@as(f32, 30), result.z);
}

test "transform translate relative" {
    var t = Transform.fromPosition(Vec3.init(1, 2, 3));
    t.translate(Vec3.init(4, 5, 6));
    try std.testing.expectEqual(@as(f32, 5), t.position.x);
    try std.testing.expectEqual(@as(f32, 7), t.position.y);
    try std.testing.expectEqual(@as(f32, 9), t.position.z);
}

test "transform forward direction" {
    const t = Transform{};
    const f = t.forward();
    try std.testing.expectEqual(@as(f32, 0), f.x);
    try std.testing.expectEqual(@as(f32, 0), f.y);
    try std.testing.expectEqual(@as(f32, -1), f.z);
}

test "transform rotation changes forward" {
    var t = Transform{};
    t.rotate(Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.pi / 2.0));
    const f = t.forward();
    // Right-hand rule: +90° around Y rotates (0,0,-1) → (-1,0,0)
    try std.testing.expectApproxEqAbs(@as(f32, -1), f.x, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0), f.y, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0), f.z, 0.001);
}

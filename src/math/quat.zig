//! Quaternion for 3D rotations.
//!
//! Used for smooth interpolation and gimbal-lock-free rotation.

const std = @import("std");
const Vec3 = @import("vec3.zig").Vec3;

/// Quaternion representing a 3D rotation.
pub const Quat = struct {
    x: f32 = 0,
    y: f32 = 0,
    z: f32 = 0,
    w: f32 = 1,

    pub const identity: Quat = .{ .w = 1 };

    /// Create quaternion from axis-angle.
    pub fn fromAxisAngle(axis: Vec3, angle: f32) Quat {
        const half = angle / 2;
        const s = @sin(half);
        const n = Vec3.normalize(axis);
        return .{
            .x = n.x * s,
            .y = n.y * s,
            .z = n.z * s,
            .w = @cos(half),
        };
    }

    /// Quaternion multiplication (composition).
    pub fn mul(a: Quat, b: Quat) Quat {
        return .{
            .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            .y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            .z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        };
    }

    /// Rotate a vector by the quaternion.
    pub fn rotate(q: Quat, v: Vec3) Vec3 {
        const qv = Vec3.init(q.x, q.y, q.z);
        const t = Vec3.scale(Vec3.cross(qv, v), 2);
        return Vec3.add(v, Vec3.add(Vec3.scale(t, q.w), Vec3.cross(qv, t)));
    }

    /// Normalize the quaternion.
    pub fn normalize(q: Quat) Quat {
        const len = @sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (len == 0) return identity;
        return .{
            .x = q.x / len,
            .y = q.y / len,
            .z = q.z / len,
            .w = q.w / len,
        };
    }

    /// Conjugate (inverse for unit quaternions).
    pub fn conjugate(q: Quat) Quat {
        return .{
            .x = -q.x,
            .y = -q.y,
            .z = -q.z,
            .w = q.w,
        };
    }

    /// Spherical linear interpolation.
    pub fn slerp(a: Quat, b: Quat, t: f32) Quat {
        var cos_angle = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        var b2 = b;
        if (cos_angle < 0) {
            cos_angle = -cos_angle;
            b2 = .{ .x = -b.x, .y = -b.y, .z = -b.z, .w = -b.w };
        }
        if (cos_angle > 0.999) {
            return normalize(.{
                .x = a.x + (b2.x - a.x) * t,
                .y = a.y + (b2.y - a.y) * t,
                .z = a.z + (b2.z - a.z) * t,
                .w = a.w + (b2.w - a.w) * t,
            });
        }
        const angle = std.math.acos(cos_angle);
        const sin_angle = @sin(angle);
        const inv_sin = 1.0 / sin_angle;
        const t1 = @sin((1 - t) * angle) * inv_sin;
        const t2 = @sin(t * angle) * inv_sin;
        return .{
            .x = a.x * t1 + b2.x * t2,
            .y = a.y * t1 + b2.y * t2,
            .z = a.z * t1 + b2.z * t2,
            .w = a.w * t1 + b2.w * t2,
        };
    }
};

test "quat identity" {
    const q = Quat.identity;
    const v = Vec3.init(1, 2, 3);
    const result = Quat.rotate(q, v);
    try std.testing.expectEqual(@as(f32, 1), result.x);
    try std.testing.expectEqual(@as(f32, 2), result.y);
    try std.testing.expectEqual(@as(f32, 3), result.z);
}

test "quat axis angle 90 degrees" {
    const q = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.pi / 2.0);
    const v = Vec3.init(1, 0, 0);
    const result = Quat.rotate(q, v);
    try std.testing.expectApproxEqAbs(@as(f32, 0), result.x, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0), result.y, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, -1), result.z, 0.001);
}

test "quat composition" {
    const q1 = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.pi / 4.0);
    const q2 = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.pi / 4.0);
    const combined = Quat.mul(q1, q2);
    const v = Vec3.init(1, 0, 0);
    const result = Quat.rotate(combined, v);
    try std.testing.expectApproxEqAbs(@as(f32, 0), result.x, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0), result.y, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, -1), result.z, 0.001);
}

test "quat slerp" {
    const q1 = Quat.identity;
    const q2 = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.pi);
    const result = Quat.slerp(q1, q2, 0.5);
    // t=0.5 slerp between identity and 180° Y-rotation → 90° Y-rotation
    // Rotating (1,0,0) by 90° around Y → (0,0,1) in this convention
    const v = Vec3.init(1, 0, 0);
    const rotated = Quat.rotate(result, v);
    try std.testing.expectApproxEqAbs(@as(f32, 0), rotated.x, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0), rotated.y, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 1), rotated.z, 0.001);
}

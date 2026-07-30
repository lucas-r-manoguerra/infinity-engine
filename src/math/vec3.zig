//! 3D vector type with common operations.

const std = @import("std");

/// 3D vector with x, y, z components.
pub const Vec3 = struct {
    x: f32 = 0,
    y: f32 = 0,
    z: f32 = 0,

    pub const zero: Vec3 = .{};
    pub const one: Vec3 = .{ .x = 1, .y = 1, .z = 1 };
    pub const right: Vec3 = .{ .x = 1, .y = 0, .z = 0 };
    pub const up: Vec3 = .{ .x = 0, .y = 1, .z = 0 };
    pub const forward: Vec3 = .{ .x = 0, .y = 0, .z = -1 };

    /// Create a new vector.
    pub fn init(x: f32, y: f32, z: f32) Vec3 {
        return .{ .x = x, .y = y, .z = z };
    }

    /// Vector addition.
    pub fn add(a: Vec3, b: Vec3) Vec3 {
        return .{
            .x = a.x + b.x,
            .y = a.y + b.y,
            .z = a.z + b.z,
        };
    }

    /// Vector subtraction.
    pub fn sub(a: Vec3, b: Vec3) Vec3 {
        return .{
            .x = a.x - b.x,
            .y = a.y - b.y,
            .z = a.z - b.z,
        };
    }

    /// Scalar multiplication.
    pub fn scale(v: Vec3, s: f32) Vec3 {
        return .{
            .x = v.x * s,
            .y = v.y * s,
            .z = v.z * s,
        };
    }

    /// Dot product.
    pub fn dot(a: Vec3, b: Vec3) f32 {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    /// Cross product.
    pub fn cross(a: Vec3, b: Vec3) Vec3 {
        return .{
            .x = a.y * b.z - a.z * b.y,
            .y = a.z * b.x - a.x * b.z,
            .z = a.x * b.y - a.y * b.x,
        };
    }

    /// Squared length (avoids sqrt).
    pub fn lengthSq(v: Vec3) f32 {
        return dot(v, v);
    }

    /// Length (magnitude).
    pub fn length(v: Vec3) f32 {
        return @sqrt(lengthSq(v));
    }

    /// Normalize the vector. Returns zero vector if length is zero.
    pub fn normalize(v: Vec3) Vec3 {
        const len = length(v);
        if (len == 0) return zero;
        return scale(v, 1.0 / len);
    }

    /// Linear interpolation between a and b.
    pub fn lerp(a: Vec3, b: Vec3, t: f32) Vec3 {
        return .{
            .x = a.x + (b.x - a.x) * t,
            .y = a.y + (b.y - a.y) * t,
            .z = a.z + (b.z - a.z) * t,
        };
    }

    /// Negate the vector.
    pub fn negate(v: Vec3) Vec3 {
        return .{ .x = -v.x, .y = -v.y, .z = -v.z };
    }

    /// Component-wise multiplication (Hadamard product).
    pub fn hadamard(a: Vec3, b: Vec3) Vec3 {
        return .{
            .x = a.x * b.x,
            .y = a.y * b.y,
            .z = a.z * b.z,
        };
    }

    /// Distance between two points.
    pub fn distance(a: Vec3, b: Vec3) f32 {
        return length(sub(a, b));
    }

    pub fn format(self: Vec3, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;
        try writer.print("Vec3({d:.3}, {d:.3}, {d:.3})", .{ self.x, self.y, self.z });
    }
};

test "vec3 addition" {
    const a = Vec3.init(1, 2, 3);
    const b = Vec3.init(4, 5, 6);
    const result = Vec3.add(a, b);
    try std.testing.expectEqual(@as(f32, 5), result.x);
    try std.testing.expectEqual(@as(f32, 7), result.y);
    try std.testing.expectEqual(@as(f32, 9), result.z);
}

test "vec3 subtraction" {
    const a = Vec3.init(5, 5, 5);
    const b = Vec3.init(1, 2, 3);
    const result = Vec3.sub(a, b);
    try std.testing.expectEqual(@as(f32, 4), result.x);
    try std.testing.expectEqual(@as(f32, 3), result.y);
    try std.testing.expectEqual(@as(f32, 2), result.z);
}

test "vec3 dot product" {
    const a = Vec3.init(1, 0, 0);
    const b = Vec3.init(0, 1, 0);
    try std.testing.expectEqual(@as(f32, 0), Vec3.dot(a, b));

    const c = Vec3.init(2, 3, 4);
    const d = Vec3.init(1, 1, 1);
    try std.testing.expectEqual(@as(f32, 9), Vec3.dot(c, d));
}

test "vec3 cross product" {
    const a = Vec3.init(1, 0, 0);
    const b = Vec3.init(0, 1, 0);
    const result = Vec3.cross(a, b);
    try std.testing.expectEqual(@as(f32, 0), result.x);
    try std.testing.expectEqual(@as(f32, 0), result.y);
    try std.testing.expectEqual(@as(f32, 1), result.z);
}

test "vec3 length and normalize" {
    const v = Vec3.init(3, 4, 0);
    try std.testing.expectEqual(@as(f32, 5), Vec3.length(v));
    try std.testing.expectEqual(@as(f32, 25), Vec3.lengthSq(v));

    const n = Vec3.normalize(v);
    try std.testing.expectApproxEqAbs(@as(f32, 0.6), n.x, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0.8), n.y, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0.0), n.z, 0.001);
}

test "vec3 lerp" {
    const a = Vec3.init(0, 0, 0);
    const b = Vec3.init(10, 10, 10);
    const result = Vec3.lerp(a, b, 0.5);
    try std.testing.expectEqual(@as(f32, 5), result.x);
    try std.testing.expectEqual(@as(f32, 5), result.y);
    try std.testing.expectEqual(@as(f32, 5), result.z);
}

test "vec3 zero vector normalize returns zero" {
    const result = Vec3.normalize(Vec3.zero);
    try std.testing.expectEqual(@as(f32, 0), result.x);
    try std.testing.expectEqual(@as(f32, 0), result.y);
    try std.testing.expectEqual(@as(f32, 0), result.z);
}

test "vec3 scale" {
    const v = Vec3.init(2, 3, 4);
    const result = Vec3.scale(v, 2);
    try std.testing.expectEqual(@as(f32, 4), result.x);
    try std.testing.expectEqual(@as(f32, 6), result.y);
    try std.testing.expectEqual(@as(f32, 8), result.z);
}

test "vec3 distance" {
    const a = Vec3.init(0, 0, 0);
    const b = Vec3.init(3, 4, 0);
    try std.testing.expectEqual(@as(f32, 5), Vec3.distance(a, b));
}

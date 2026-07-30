//! 4D vector type.

const std = @import("std");

/// 4D vector with x, y, z, w components.
pub const Vec4 = struct {
    x: f32 = 0,
    y: f32 = 0,
    z: f32 = 0,
    w: f32 = 1,

    pub const zero: Vec4 = .{};
    pub const one: Vec4 = .{ .x = 1, .y = 1, .z = 1, .w = 1 };

    pub fn init(x: f32, y: f32, z: f32, w: f32) Vec4 {
        return .{ .x = x, .y = y, .z = z, .w = w };
    }

    /// Convert to Vec3 (drop w).
    pub fn xyz(v: Vec4) @import("vec3.zig").Vec3 {
        return .{ .x = v.x, .y = v.y, .z = v.z };
    }

    pub fn format(self: Vec4, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;
        try writer.print("Vec4({d:.3}, {d:.3}, {d:.3}, {d:.3})", .{ self.x, self.y, self.z, self.w });
    }
};

test "vec4 init" {
    const v = Vec4.init(1, 2, 3, 4);
    try std.testing.expectEqual(@as(f32, 1), v.x);
    try std.testing.expectEqual(@as(f32, 4), v.w);
}

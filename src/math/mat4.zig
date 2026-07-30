//! 4x4 matrix for 3D transformations and projections.

const std = @import("std");
const Vec3 = @import("vec3.zig").Vec3;
const Quat = @import("quat.zig").Quat;

/// 4x4 column-major matrix.
pub const Mat4 = struct {
    data: [16]f32 = [_]f32{0} ** 16,

    pub const identity: Mat4 = .{
        .data = .{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        },
    };

    /// Create from raw column-major data.
    pub fn fromSlice(data: [16]f32) Mat4 {
        return .{ .data = data };
    }

    /// Matrix multiplication: a * b.
    pub fn mul(a: Mat4, b: Mat4) Mat4 {
        var result: Mat4 = .{};
        var col: u32 = 0;
        while (col < 4) : (col += 1) {
            var row: u32 = 0;
            while (row < 4) : (row += 1) {
                var sum: f32 = 0;
                var k: u32 = 0;
                while (k < 4) : (k += 1) {
                    sum += a.data[k * 4 + row] * b.data[col * 4 + k];
                }
                result.data[col * 4 + row] = sum;
            }
        }
        return result;
    }

    /// Transform a Vec3 by the matrix (w = 1, full transform).
    pub fn transformPoint(m: Mat4, v: Vec3) Vec3 {
        const x = m.data[0] * v.x + m.data[4] * v.y + m.data[8] * v.z + m.data[12];
        const y = m.data[1] * v.x + m.data[5] * v.y + m.data[9] * v.z + m.data[13];
        const z = m.data[2] * v.x + m.data[6] * v.y + m.data[10] * v.z + m.data[14];
        const w = m.data[3] * v.x + m.data[7] * v.y + m.data[11] * v.z + m.data[15];
        if (w == 0 or w == 1) return Vec3.init(x, y, z);
        return Vec3.init(x / w, y / w, z / w);
    }

    /// Transform a Vec3 direction (w = 0, no translation).
    pub fn transformDirection(m: Mat4, v: Vec3) Vec3 {
        return Vec3.init(
            m.data[0] * v.x + m.data[4] * v.y + m.data[8] * v.z,
            m.data[1] * v.x + m.data[5] * v.y + m.data[9] * v.z,
            m.data[2] * v.x + m.data[6] * v.y + m.data[10] * v.z,
        );
    }

    /// Create a translation matrix.
    pub fn translate(v: Vec3) Mat4 {
        var m = identity;
        m.data[12] = v.x;
        m.data[13] = v.y;
        m.data[14] = v.z;
        return m;
    }

    /// Create a rotation matrix around X axis.
    pub fn rotateX(angle: f32) Mat4 {
        const c = @cos(angle);
        const s = @sin(angle);
        var m = identity;
        m.data[5] = c;
        m.data[6] = s;
        m.data[9] = -s;
        m.data[10] = c;
        return m;
    }

    /// Create a rotation matrix around Y axis.
    pub fn rotateY(angle: f32) Mat4 {
        const c = @cos(angle);
        const s = @sin(angle);
        var m = identity;
        m.data[0] = c;
        m.data[2] = -s;
        m.data[8] = s;
        m.data[10] = c;
        return m;
    }

    /// Create a rotation matrix around Z axis.
    pub fn rotateZ(angle: f32) Mat4 {
        const c = @cos(angle);
        const s = @sin(angle);
        var m = identity;
        m.data[0] = c;
        m.data[1] = s;
        m.data[4] = -s;
        m.data[5] = c;
        return m;
    }

    /// Create a scaling matrix.
    pub fn scale(v: Vec3) Mat4 {
        var m = identity;
        m.data[0] = v.x;
        m.data[5] = v.y;
        m.data[10] = v.z;
        return m;
    }

    /// Create a perspective projection matrix.
    /// fov: vertical field of view in radians
    /// aspect: width / height
    /// near: near clip plane
    /// far: far clip plane
    pub fn perspective(fov: f32, aspect: f32, near: f32, far: f32) Mat4 {
        const f = 1.0 / @tan(fov / 2.0);
        const range_inv = 1.0 / (near - far);
        var m: Mat4 = .{};
        m.data[0] = f / aspect;
        m.data[5] = f;
        m.data[10] = (near + far) * range_inv;
        m.data[11] = -1;
        m.data[14] = 2.0 * near * far * range_inv;
        return m;
    }

    /// Create an orthographic projection matrix.
    pub fn ortho(left: f32, right: f32, bottom: f32, top: f32, near: f32, far: f32) Mat4 {
        var m: Mat4 = .{};
        m.data[0] = 2.0 / (right - left);
        m.data[5] = 2.0 / (top - bottom);
        m.data[10] = -2.0 / (far - near);
        m.data[12] = -(right + left) / (right - left);
        m.data[13] = -(top + bottom) / (top - bottom);
        m.data[14] = -(far + near) / (far - near);
        m.data[15] = 1;
        return m;
    }

    /// Look-at view matrix.
    pub fn lookAt(eye: Vec3, target: Vec3, up: Vec3) Mat4 {
        const f = Vec3.normalize(Vec3.sub(target, eye));
        const s = Vec3.normalize(Vec3.cross(f, up));
        const u = Vec3.cross(s, f);

        var m = identity;
        m.data[0] = s.x;
        m.data[1] = u.x;
        m.data[2] = -f.x;
        m.data[4] = s.y;
        m.data[5] = u.y;
        m.data[6] = -f.y;
        m.data[8] = s.z;
        m.data[9] = u.z;
        m.data[10] = -f.z;
        m.data[12] = -Vec3.dot(s, eye);
        m.data[13] = -Vec3.dot(u, eye);
        m.data[14] = Vec3.dot(f, eye);
        return m;
    }

    /// Transpose the matrix.
    pub fn transpose(m: Mat4) Mat4 {
        var result: Mat4 = .{};
        var col: u32 = 0;
        while (col < 4) : (col += 1) {
            var row: u32 = 0;
            while (row < 4) : (row += 1) {
                result.data[col * 4 + row] = m.data[row * 4 + col];
            }
        }
        return result;
    }

    /// Compute the inverse of a 4x4 matrix using the adjugate method.
    /// Returns identity for singular matrices (determinant near zero).
    pub fn inverse(m: Mat4) Mat4 {
        const a = m.data[0]; const b = m.data[4]; const c = m.data[8];  const d = m.data[12];
        const e = m.data[1]; const f = m.data[5]; const g = m.data[9];  const h = m.data[13];
        const i = m.data[2]; const j = m.data[6]; const k = m.data[10]; const l = m.data[14];
        const m3 = m.data[3]; const n = m.data[7]; const o = m.data[11]; const p = m.data[15];

        const c00 = f * (k * p - l * o) - g * (j * p - l * n) + h * (j * o - k * n);
        const c01 = -(e * (k * p - l * o) - g * (i * p - l * m3) + h * (i * o - k * m3));
        const c02 = e * (j * p - l * n) - f * (i * p - l * m3) + h * (i * n - j * m3);
        const c03 = -(e * (j * o - k * n) - f * (i * o - k * m3) + g * (i * n - j * m3));

        const c10 = -(b * (k * p - l * o) - c * (j * p - l * n) + d * (j * o - k * n));
        const c11 = a * (k * p - l * o) - c * (i * p - l * m3) + d * (i * o - k * m3);
        const c12 = -(a * (j * p - l * n) - b * (i * p - l * m3) + d * (i * n - j * m3));
        const c13 = a * (j * o - k * n) - b * (i * o - k * m3) + c * (i * n - j * m3);

        const c20 = b * (g * p - h * o) - c * (f * p - h * n) + d * (f * o - g * n);
        const c21 = -(a * (g * p - h * o) - c * (e * p - h * m3) + d * (e * o - g * m3));
        const c22 = a * (f * p - h * n) - b * (e * p - h * m3) + d * (e * n - f * m3);
        const c23 = -(a * (f * o - g * n) - b * (e * o - g * m3) + c * (e * n - f * m3));

        const c30 = -(b * (g * l - h * k) - c * (f * l - h * j) + d * (f * k - g * j));
        const c31 = a * (g * l - h * k) - c * (e * l - h * i) + d * (e * k - g * i);
        const c32 = -(a * (f * l - h * j) - b * (e * l - h * i) + d * (e * j - f * i));
        const c33 = a * (f * k - g * j) - b * (e * k - g * i) + c * (e * j - f * i);

        const det = a * c00 + b * c01 + c * c02 + d * c03;
        if (@abs(det) < 1e-9) return identity;

        const inv_det = 1.0 / det;
        var result: Mat4 = .{};
        result.data[0] = c00 * inv_det;
        result.data[1] = c01 * inv_det;
        result.data[2] = c02 * inv_det;
        result.data[3] = c03 * inv_det;
        result.data[4] = c10 * inv_det;
        result.data[5] = c11 * inv_det;
        result.data[6] = c12 * inv_det;
        result.data[7] = c13 * inv_det;
        result.data[8] = c20 * inv_det;
        result.data[9] = c21 * inv_det;
        result.data[10] = c22 * inv_det;
        result.data[11] = c23 * inv_det;
        result.data[12] = c30 * inv_det;
        result.data[13] = c31 * inv_det;
        result.data[14] = c32 * inv_det;
        result.data[15] = c33 * inv_det;
        return result;
    }

    /// Build a rotation matrix from a quaternion.
    pub fn fromQuat(q: Quat) Mat4 {
        const xx = q.x * q.x;
        const yy = q.y * q.y;
        const zz = q.z * q.z;
        const xy = q.x * q.y;
        const xz = q.x * q.z;
        const yz = q.y * q.z;
        const wx = q.w * q.x;
        const wy = q.w * q.y;
        const wz = q.w * q.z;

        var m = identity;
        m.data[0] = 1 - 2 * (yy + zz);
        m.data[1] = 2 * (xy + wz);
        m.data[2] = 2 * (xz - wy);
        m.data[4] = 2 * (xy - wz);
        m.data[5] = 1 - 2 * (xx + zz);
        m.data[6] = 2 * (yz + wx);
        m.data[8] = 2 * (xz + wy);
        m.data[9] = 2 * (yz - wx);
        m.data[10] = 1 - 2 * (xx + yy);
        return m;
    }

    /// Access element by column and row.
    pub fn at(m: Mat4, col: usize, row: usize) f32 {
        return m.data[col * 4 + row];
    }

    pub fn format(self: Mat4, comptime fmt: []const u8, options: std.fmt.FormatOptions, writer: anytype) !void {
        _ = fmt;
        _ = options;
        try writer.print("Mat4\n", .{});
        var row: u32 = 0;
        while (row < 4) : (row += 1) {
            try writer.print("  [{d:7.3} {d:7.3} {d:7.3} {d:7.3}]\n", .{
                self.data[0 * 4 + row],
                self.data[1 * 4 + row],
                self.data[2 * 4 + row],
                self.data[3 * 4 + row],
            });
        }
    }
};

test "mat4 identity" {
    const m = Mat4.identity;
    try std.testing.expectEqual(@as(f32, 1), m.data[0]);
    try std.testing.expectEqual(@as(f32, 1), m.data[5]);
    try std.testing.expectEqual(@as(f32, 1), m.data[10]);
    try std.testing.expectEqual(@as(f32, 1), m.data[15]);
}

test "mat4 translation" {
    const t = Mat4.translate(Vec3.init(10, 20, 30));
    try std.testing.expectEqual(@as(f32, 10), t.data[12]);
    try std.testing.expectEqual(@as(f32, 20), t.data[13]);
    try std.testing.expectEqual(@as(f32, 30), t.data[14]);
}

test "mat4 transform point" {
    const t = Mat4.translate(Vec3.init(5, 10, 15));
    const p = Vec3.init(1, 2, 3);
    const result = Mat4.transformPoint(t, p);
    try std.testing.expectEqual(@as(f32, 6), result.x);
    try std.testing.expectEqual(@as(f32, 12), result.y);
    try std.testing.expectEqual(@as(f32, 18), result.z);
}

test "mat4 multiplication" {
    const t1 = Mat4.translate(Vec3.init(1, 2, 3));
    const t2 = Mat4.translate(Vec3.init(4, 5, 6));
    const combined = Mat4.mul(t1, t2);
    const p = Vec3.init(0, 0, 0);
    const result = Mat4.transformPoint(combined, p);
    try std.testing.expectEqual(@as(f32, 5), result.x);
    try std.testing.expectEqual(@as(f32, 7), result.y);
    try std.testing.expectEqual(@as(f32, 9), result.z);
}

test "mat4 scale" {
    const s = Mat4.scale(Vec3.init(2, 3, 4));
    const p = Vec3.init(1, 1, 1);
    const result = Mat4.transformPoint(s, p);
    try std.testing.expectEqual(@as(f32, 2), result.x);
    try std.testing.expectEqual(@as(f32, 3), result.y);
    try std.testing.expectEqual(@as(f32, 4), result.z);
}

test "mat4 perspective" {
    const proj = Mat4.perspective(1.0, 16.0 / 9.0, 0.1, 100.0);
    // Check that projection matrix is not identity
    try std.testing.expect(proj.data[0] != 1);
    try std.testing.expect(proj.data[11] == -1);
}

test "mat4 ortho" {
    const proj = Mat4.ortho(-1, 1, -1, 1, -1, 1);
    const p = Vec3.init(0.5, 0.5, 0.5);
    const result = Mat4.transformPoint(proj, p);
    try std.testing.expectApproxEqAbs(@as(f32, 0.5), result.x, 0.001);
}

test "mat4 transpose" {
    // Row-major: fromSlice fills row by row
    const m = Mat4.fromSlice(.{
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16,
    });
    const t = Mat4.transpose(m);
    // Transposed row-major:
    // row 0: 1, 5, 9,  13
    // row 1: 2, 6, 10, 14
    // row 2: 3, 7, 11, 15
    // row 3: 4, 8, 12, 16
    try std.testing.expectEqual(@as(f32, 1), t.data[0]);
    try std.testing.expectEqual(@as(f32, 5), t.data[1]);
    try std.testing.expectEqual(@as(f32, 2), t.data[4]);
    try std.testing.expectEqual(@as(f32, 6), t.data[5]);
}

test "mat4 rotate" {
    const r = Mat4.rotateY(std.math.pi / 2.0);
    const p = Vec3.init(1, 0, 0);
    const result = Mat4.transformPoint(r, p);
    try std.testing.expectApproxEqAbs(@as(f32, 0), result.x, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0), result.y, 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, -1), result.z, 0.001);
}

test "mat4 inverse" {
    const t = Mat4.translate(Vec3.init(5, 10, 15));
    const inv = Mat4.inverse(t);
    const result = Mat4.mul(t, inv);
    try std.testing.expectApproxEqAbs(@as(f32, 1), result.data[0], 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 1), result.data[5], 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 1), result.data[10], 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 1), result.data[15], 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0), result.data[12], 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0), result.data[13], 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 0), result.data[14], 0.001);
}

test "mat4 inverse identity" {
    const inv = Mat4.inverse(Mat4.identity);
    try std.testing.expectEqual(@as(f32, 1), inv.data[0]);
    try std.testing.expectEqual(@as(f32, 1), inv.data[5]);
    try std.testing.expectEqual(@as(f32, 1), inv.data[10]);
    try std.testing.expectEqual(@as(f32, 1), inv.data[15]);
}

test "mat4 inverse rotation" {
    const r = Mat4.rotateY(0.7);
    const inv = Mat4.inverse(r);
    const result = Mat4.mul(r, inv);
    try std.testing.expectApproxEqAbs(@as(f32, 1), result.data[0], 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 1), result.data[5], 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 1), result.data[10], 0.001);
    try std.testing.expectApproxEqAbs(@as(f32, 1), result.data[15], 0.001);
}

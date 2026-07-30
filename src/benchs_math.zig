//! Benchmarks: math/ (Vec3, Mat4, Quat, Transform)
//! Priority P0 — called millions of times per frame.
//!
//! Run via: `zig build bench`

const std = @import("std");
const time = @import("core/time.zig");
const Vec3 = @import("math/vec3.zig").Vec3;
const Mat4 = @import("math/mat4.zig").Mat4;
const Quat = @import("math/quat.zig").Quat;
const Transform = @import("math/transform.zig").Transform;

const ITERATIONS = 1_000_000;

// Global sink — prevents DCE of computation results.
var sink: u32 = 0;

fn hashF32(x: f32) u32 { return @as(u32, @bitCast(x)); }
fn hashVec3(v: Vec3) u32 { return hashF32(v.x) ^ hashF32(v.y) ^ hashF32(v.z); }
fn hashMat4(m: Mat4) u32 { return hashF32(m.data[0]) ^ hashF32(m.data[5]); }
fn hashQuat(q: Quat) u32 { return hashF32(q.x) ^ hashF32(q.y) ^ hashF32(q.z) ^ hashF32(q.w); }

// ---------------------------------------------------------------------------
// Vec3
// ---------------------------------------------------------------------------

fn benchVec3Add(i: usize) void {
    const a = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 2, 3);
    const b = Vec3.init(4, 5, 6);
    sink +%= hashVec3(Vec3.add(a, b));
}

fn benchVec3Sub(i: usize) void {
    const a = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 2, 3);
    const b = Vec3.init(4, 5, 6);
    sink +%= hashVec3(Vec3.sub(a, b));
}

fn benchVec3Scale(i: usize) void {
    const a = Vec3.init(1, @as(f32, @floatFromInt(i & 0xFF)), 3);
    sink +%= hashVec3(Vec3.scale(a, 2.0));
}

fn benchVec3Dot(i: usize) void {
    const a = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 2, 3);
    const b = Vec3.init(4, 5, 6);
    sink +%= hashF32(Vec3.dot(a, b));
}

fn benchVec3Cross(i: usize) void {
    const a = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 2, 3);
    const b = Vec3.init(4, 5, 6);
    sink +%= hashVec3(Vec3.cross(a, b));
}

fn benchVec3Length(i: usize) void {
    const a = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 4, 5);
    sink +%= hashF32(Vec3.length(a));
}

fn benchVec3Normalize(i: usize) void {
    const a = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 4, 5);
    sink +%= hashVec3(Vec3.normalize(a));
}

fn benchVec3Lerp(i: usize) void {
    const a = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 2, 3);
    const b = Vec3.init(4, 5, 6);
    sink +%= hashVec3(Vec3.lerp(a, b, 0.5));
}

fn benchVec3Hadamard(i: usize) void {
    const a = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 2, 3);
    const b = Vec3.init(4, 5, 6);
    sink +%= hashVec3(Vec3.hadamard(a, b));
}

// ---------------------------------------------------------------------------
// Mat4
// ---------------------------------------------------------------------------

fn benchMat4Mul(i: usize) void {
    const a = Mat4.translate(Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 2, 3));
    const b = Mat4.rotateX(std.math.degreesToRadians(@as(f32, @floatFromInt(i & 0x3F))));
    sink +%= hashMat4(Mat4.mul(a, b));
}

fn benchMat4TransformPoint(i: usize) void {
    const m = Mat4.translate(Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 20, 30));
    const v = Vec3.init(1, 2, 3);
    sink +%= hashVec3(Mat4.transformPoint(m, v));
}

fn benchMat4TransformDirection(i: usize) void {
    const m = Mat4.rotateY(std.math.degreesToRadians(@as(f32, @floatFromInt(i & 0x3F))));
    const v = Vec3.init(1, 0, 0);
    sink +%= hashVec3(Mat4.transformDirection(m, v));
}

fn benchMat4Inverse(i: usize) void {
    const m = Mat4.perspective(std.math.degreesToRadians(@as(f32, @floatFromInt(60 + (i & 0xF)))), 16.0 / 9.0, 0.1, 100.0);
    sink +%= hashMat4(Mat4.inverse(m));
}

fn benchMat4Transpose(i: usize) void {
    const m = Mat4.translate(Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 2, 3));
    const r = Mat4.transpose(m);
    sink +%= hashF32(r.data[3]) ^ hashF32(r.data[7]);
}

fn benchMat4FromQuat(i: usize) void {
    const q = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.degreesToRadians(@as(f32, @floatFromInt(45 + (i & 0x3F)))));
    sink +%= hashMat4(Mat4.fromQuat(q));
}

// ---------------------------------------------------------------------------
// Quat
// ---------------------------------------------------------------------------

fn benchQuatMul(i: usize) void {
    const a = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.degreesToRadians(@as(f32, @floatFromInt(45 + (i & 0x3F)))));
    const b = Quat.fromAxisAngle(Vec3.init(1, 0, 0), std.math.degreesToRadians(@as(f32, @floatFromInt(30 + (i & 0x3F)))));
    sink +%= hashQuat(Quat.mul(a, b));
}

fn benchQuatRotate(i: usize) void {
    const q = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.degreesToRadians(@as(f32, @floatFromInt(90 + (i & 0x3F)))));
    const v = Vec3.init(1, 0, 0);
    sink +%= hashVec3(Quat.rotate(q, v));
}

fn benchQuatNormalize(i: usize) void {
    const q = Quat.fromAxisAngle(Vec3.init(1, 1, 0), std.math.degreesToRadians(@as(f32, @floatFromInt(45 + (i & 0x3F)))));
    sink +%= hashQuat(Quat.normalize(q));
}

fn benchQuatSlerp(i: usize) void {
    const a = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.degreesToRadians(@as(f32, @floatFromInt(i & 0x3F))));
    const b = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.degreesToRadians(@as(f32, @floatFromInt(90 + (i & 0x3F)))));
    sink +%= hashQuat(Quat.slerp(a, b, 0.5));
}

fn benchQuatFromAxisAngle(i: usize) void {
    const axis = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 1, 0);
    const angle = std.math.degreesToRadians(@as(f32, @floatFromInt(45 + (i & 0x3F))));
    sink +%= hashQuat(Quat.fromAxisAngle(axis, angle));
}

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------

fn benchTransformToMatrix(i: usize) void {
    var xf = Transform.fromPosition(Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 20, 30));
    xf.rotation = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.degreesToRadians(@as(f32, @floatFromInt(45 + (i & 0x3F)))));
    xf.scale = Vec3.init(2, 2, 2);
    sink +%= hashMat4(xf.toMatrix());
}

fn benchTransformLookAt(i: usize) void {
    var xf = Transform.fromPosition(Vec3.init(0, 0, 0));
    const target = Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 5, 0);
    xf.lookAt(target, Vec3.up);
    sink +%= hashQuat(xf.rotation);
}

fn benchTransformForward(i: usize) void {
    var xf = Transform.fromPosition(Vec3.init(@as(f32, @floatFromInt(i & 0xFF)), 0, 0));
    xf.rotation = Quat.fromAxisAngle(Vec3.init(0, 1, 0), std.math.degreesToRadians(@as(f32, @floatFromInt(45 + (i & 0x3F)))));
    sink +%= hashVec3(xf.forward());
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

pub fn run() void {
    std.debug.print("\n  ~ math benchmarks ({d} iterations each, ReleaseSafe) ~\n", .{ITERATIONS});

    bench("vec3.add", benchVec3Add);
    bench("vec3.sub", benchVec3Sub);
    bench("vec3.scale", benchVec3Scale);
    bench("vec3.dot", benchVec3Dot);
    bench("vec3.cross", benchVec3Cross);
    bench("vec3.length", benchVec3Length);
    bench("vec3.normalize", benchVec3Normalize);
    bench("vec3.lerp", benchVec3Lerp);
    bench("vec3.hadamard", benchVec3Hadamard);

    bench("mat4.mul", benchMat4Mul);
    bench("mat4.transformPoint", benchMat4TransformPoint);
    bench("mat4.transformDirection", benchMat4TransformDirection);
    bench("mat4.inverse", benchMat4Inverse);
    bench("mat4.transpose", benchMat4Transpose);
    bench("mat4.fromQuat", benchMat4FromQuat);

    bench("quat.mul", benchQuatMul);
    bench("quat.rotate", benchQuatRotate);
    bench("quat.normalize", benchQuatNormalize);
    bench("quat.slerp", benchQuatSlerp);
    bench("quat.fromAxisAngle", benchQuatFromAxisAngle);

    bench("transform.toMatrix", benchTransformToMatrix);
    bench("transform.lookAt", benchTransformLookAt);
    bench("transform.forward", benchTransformForward);

    std.debug.print("  (sink={d} — sentinel)\n", .{sink});
}

fn bench(comptime name: []const u8, comptime func: *const fn (usize) void) void {
    // warmup
    func(0);

    const start = time.nanoTime();
    for (0..ITERATIONS) |i| {
        func(i);
    }
    const elapsed = time.nanoTime() - start;
    const elapsed_f: f64 = @as(f64, @floatFromInt(elapsed));
    const iters_f: f64 = @as(f64, @floatFromInt(ITERATIONS));
    const ns_per_op = elapsed_f / iters_f;
    std.debug.print("  {s: <30}  {d: >8.1} ns/op\n", .{ name, ns_per_op });
}

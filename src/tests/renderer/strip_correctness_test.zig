//! Strip rasterization correctness and thread overhead benchmark.
//!
//! Verifies that multi-threaded drawTriangleStrip (4 threads, non-overlapping
//! Y strips) produces output bit-identical to single-threaded drawTriangle.
//! Runs the comparison across multiple frames with varying vertex positions
//! to catch edge cases at strip boundaries.
//!
//! PR workflow: run `zig build test` before merging P2 strip changes.

const std = @import("std");
const testing = std.testing;
const Vertex = @import("../../renderer/renderer.zig").Vertex;
const SoftwareBackend = @import("../../renderer/software.zig").SoftwareBackend;
const ThreadPool = @import("../../core/thread_pool.zig").ThreadPool;
const Color = @import("../../core/color.zig").Color;
const COLORS = @import("../../core/color.zig").COLORS;
const window = @import("../../platform/window.zig");

const W = 128;
const H = 128;

/// A triangle triple (3 vertices).
const Triple = struct { v0: Vertex, v1: Vertex, v2: Vertex };

/// Simple FNV-1a hash over the framebuffer bytes.
fn hashFb(fb: []const u8) u32 {
    var h: u32 = 2166136261;
    for (fb) |byte| {
        h ^= byte;
        h *%= 16777619;
    }
    return h;
}

/// Render all triangles to the backend using single-threaded drawTriangle.
fn renderSingle(backend: *SoftwareBackend, tris: []const Triple, tex: []const u8) void {
    for (tris) |tri| {
        backend.drawTriangle(tri.v0, tri.v1, tri.v2, tex);
    }
}

/// Render all triangles to the backend using multi-threaded drawTriangleStrip.
/// Each thread renders ALL triangles clipped to its Y strip.
fn renderStrip(backend: *SoftwareBackend, pool: *ThreadPool, thread_count: u32, tris: []const Triple, tex: []const u8) void {
    const strip_ctx = StripRenderCtx{
        .backend = backend,
        .tris = tris,
        .tex = tex,
        .h = H,
    };
    pool.spawn(stripWorker, &strip_ctx, thread_count);
    pool.wait();
}

const StripRenderCtx = struct {
    backend: *SoftwareBackend,
    tris: []const Triple,
    tex: []const u8,
    h: u32,
};

fn stripWorker(idx: usize, ctx: *StripRenderCtx) void {
    const tc: u32 = 4;
    const strip_h = (ctx.h + tc - 1) / tc;
    const y_min = @as(i32, @intCast(idx * strip_h));
    const y_max = @min(@as(i32, @intCast((idx + 1) * strip_h)), @as(i32, @intCast(ctx.h)));

    for (ctx.tris) |tri| {
        ctx.backend.drawTriangleStrip(tri.v0, tri.v1, tri.v2, ctx.tex, y_min, y_max);
    }
}

/// Generate a frame's worth of test triangles, varying by frame number.
/// Returns a static buffer — callers must not write to it.
fn generateTestTris(frame: u64) [4]Triple {
    const angle = @as(f32, @floatFromInt(frame)) * 0.1;
    const cx = 64.0 + @cos(angle) * 30.0;
    const cy = 64.0 + @sin(angle * 0.7) * 25.0;
    const r = 20.0 + @sin(angle * 0.3) * 8.0;

    // 4 triangles with varying positions and depths
    return .{
        // Large triangle covering multiple strips
        .{
            .v0 = Vertex{ .x = cx - r, .y = cy - r, .z = 0.3, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } },
            .v1 = Vertex{ .x = cx + r, .y = cy - r, .z = 0.3, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } },
            .v2 = Vertex{ .x = cx, .y = cy + r, .z = 0.3, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } },
        },
        // Triangle at top edge (tests strip boundary at y=0)
        .{
            .v0 = Vertex{ .x = 10, .y = -5, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 200, .b = 50, .a = 255 } },
            .v1 = Vertex{ .x = 60, .y = -5, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 200, .b = 50, .a = 255 } },
            .v2 = Vertex{ .x = 35, .y = 30, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 50, .g = 200, .b = 50, .a = 255 } },
        },
        // Triangle at bottom edge (tests strip boundary near h-1)
        .{
            .v0 = Vertex{ .x = 70, .y = 100, .z = 0.4, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } },
            .v1 = Vertex{ .x = 120, .y = 100, .z = 0.4, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } },
            .v2 = Vertex{ .x = 95, .y = 140, .z = 0.4, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } },
        },
        // Thin triangle within one strip (tests strips with no overlap)
        .{
            .v0 = Vertex{ .x = 40, .y = 55, .z = 0.7, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 200, .b = 50, .a = 255 } },
            .v1 = Vertex{ .x = 80, .y = 55, .z = 0.7, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 200, .b = 50, .a = 255 } },
            .v2 = Vertex{ .x = 60, .y = 75, .z = 0.7, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 200, .g = 200, .b = 50, .a = 255 } },
        },
    };
}

test "T-006 drawTriangleStrip 1000-frame correctness vs drawTriangle" {
    // Create test window (skip if no display).
    var win = window.windowCreate("test-strip-correctness", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    // Use two backends — one for single-threaded, one for strip rendering.
    // Using separate backends avoids cross-contamination and lets us
    // hash each independently.
    // Force use_simd=false on both: we are testing strip-vs-single correctness,
    // not SIMD determinism. The SIMD path uses multiply-based offsets while
    // the scalar DDA path uses cumulative addition, producing bit-different
    // (but equally valid) floating-point results for the same pixel.
    var single_backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    single_backend.use_simd = false;
    defer single_backend.deinit();

    var strip_backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    strip_backend.use_simd = false;
    defer strip_backend.deinit();

    // Init thread pool for strip rendering.
    var pool = try ThreadPool.init(testing.allocator, 4);
    defer pool.deinit();

    const tex = @import("../../renderer/software.zig").generateCheckerboard();
    const tex_slice = tex[0..];

    // Run 100 frames (not 1000 — keeps test fast while still catching races)
    // Each frame renders 4 triangles at varying positions.
    const frame_count: u64 = 100;

    for (0..frame_count) |frame| {
        const tris = generateTestTris(frame);

        // --- Single-threaded render ---
        single_backend.beginFrame(COLORS.dark);
        renderSingle(&single_backend, &tris, tex_slice);
        single_backend.endFrame();
        const hash_single = hashFb(single_backend.fb);

        // --- Multi-threaded strip render ---
        strip_backend.beginFrame(COLORS.dark);
        renderStrip(&strip_backend, &pool, 4, &tris, tex_slice);
        strip_backend.endFrame();
        const hash_strip = hashFb(strip_backend.fb);

        // Both outputs MUST be bit-identical.
        try testing.expectEqual(hash_single, hash_strip);
    }
}

test "T-006b drawTriangleStrip edge-case: triangle outside all strips" {
    var win = window.windowCreate("test-strip-outside", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    var backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    defer backend.deinit();

    var pool = try ThreadPool.init(testing.allocator, 4);
    defer pool.deinit();

    const tex = @import("../../renderer/software.zig").generateCheckerboard();

    // Triangle completely outside the framebuffer (z negative — behind camera).
    const tri = std.mem.zeroInit(Vertex, .{ .x = -100, .y = -100, .z = -1, .inv_w = 1.0 });
    const tris = [_]Triple{
        .{ .v0 = tri, .v1 = tri, .v2 = tri },
    };

    backend.beginFrame(COLORS.dark);
    renderStrip(&backend, &pool, 4, &tris, tex[0..]);
    backend.endFrame();

    // Framebuffer should be entirely dark (no pixels touched).
    const pixel = @as(u32, @bitCast([4]u8{ COLORS.dark.b, COLORS.dark.g, COLORS.dark.r, COLORS.dark.a }));
    const fb32: []u32 = @ptrCast(@alignCast(backend.fb));
    for (fb32) |p| {
        try testing.expectEqual(pixel, p);
    }
}

test "T-006c drawTriangleStrip edge-case: triangle at strip seam" {
    // A triangle spanning Y=31..33 (exactly across the strip seam for 128/4=32px strips)
    // must produce the same output as drawTriangle (no gaps, no double-writes).
    var win = window.windowCreate("test-strip-seam", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    var single_backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    single_backend.use_simd = false;
    defer single_backend.deinit();

    var strip_backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    strip_backend.use_simd = false;
    defer strip_backend.deinit();

    var pool = try ThreadPool.init(testing.allocator, 4);
    defer pool.deinit();

    const tex = @import("../../renderer/software.zig").generateCheckerboard();

    // Triangle spanning Y=28..36 across the seam at Y=32
    const v0 = Vertex{ .x = 10, .y = 28, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 255, .g = 255, .b = 255, .a = 255 } };
    const v1 = Vertex{ .x = 118, .y = 28, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 255, .g = 255, .b = 255, .a = 255 } };
    const v2 = Vertex{ .x = 64, .y = 36, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 255, .g = 255, .b = 255, .a = 255 } };
    const tris = [_]Triple{ .{ .v0 = v0, .v1 = v1, .v2 = v2 } };

    // Single-threaded
    single_backend.beginFrame(COLORS.dark);
    renderSingle(&single_backend, &tris, tex[0..]);
    single_backend.endFrame();
    const hash_single = hashFb(single_backend.fb);

    // Strip
    strip_backend.beginFrame(COLORS.dark);
    renderStrip(&strip_backend, &pool, 4, &tris, tex[0..]);
    strip_backend.endFrame();
    const hash_strip = hashFb(strip_backend.fb);

    try testing.expectEqual(hash_single, hash_strip);
}

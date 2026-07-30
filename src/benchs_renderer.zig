//! Benchmarks: renderer/ (SoftwareBackend hotspots)
//! Priority P1 — software renderer has a hard resolution/complexity limit.
//!
//! Measures three triangle sizes to understand per-pixel rasterization cost,
//! then estimates renderCube (12 triangles per cube).
//!
//! NOTE: `drawTriangle` cost scales with bounding-box area tested, not pixel
//! count filled. The edge function tests EVERY pixel in the box, even ones
//! that fail the `inside` check.
//!
//! Run via: `zig build bench`

const std = @import("std");
const time = @import("core/time.zig");
const SoftwareBackend = @import("renderer/software.zig").SoftwareBackend;
const Color = @import("core/color.zig").Color;
const COLORS = @import("core/color.zig").COLORS;
const Vertex = @import("renderer/renderer.zig").Vertex;
const texture = @import("renderer/software.zig");
const window = @import("platform/window.zig");

const allocator = std.heap.page_allocator;
const WIDTH = 320;
const HEIGHT = 240;

// Single window shared across all benchmarks (created once in run()).
var bench_window: ?*window.Window = null;

// Each benchmark uses its own iteration count so total runtime stays reasonable.
const SMALL_ITERS = 10_000;
const MEDIUM_ITERS = 1_000;
const LARGE_ITERS = 100;

fn createBackend() SoftwareBackend {
    return SoftwareBackend.init(allocator, bench_window.?, WIDTH, HEIGHT) catch unreachable;
}

// ---------------------------------------------------------------------------
// Clear (beginFrame)
// ---------------------------------------------------------------------------

fn benchBeginFrame() void {
    var fb = createBackend();
    defer fb.deinit();

    for (0..1000) |_| {
        fb.beginFrame(COLORS.dark);
    }
}

// ---------------------------------------------------------------------------
// Render empty frame (beginFrame + endFrame)
// ---------------------------------------------------------------------------

fn benchEmptyFrame() void {
    var fb = createBackend();
    defer fb.deinit();

    for (0..1000) |_| {
        fb.beginFrame(COLORS.dark);
        fb.endFrame();
    }
}

// ---------------------------------------------------------------------------
// drawTriangle — small triangle (~30×30 bounding box, ~900 px tested)
//
// Approximately the size of a distant cube face.
// ---------------------------------------------------------------------------

fn benchDrawSmall() void {
    var fb = createBackend();
    defer fb.deinit();

    const v0 = Vertex{ .x = 145, .y = 210, .z = 0, .nx = 0, .ny = 0, .nz = -1, .u = 0, .v = 0, .inv_w = 1, .color = COLORS.white };
    const v1 = Vertex{ .x = 175, .y = 210, .z = 0, .nx = 0, .ny = 0, .nz = -1, .u = 1, .v = 0, .inv_w = 1, .color = COLORS.white };
    const v2 = Vertex{ .x = 160, .y = 180, .z = 0, .nx = 0, .ny = 0, .nz = -1, .u = 0.5, .v = 1, .inv_w = 1, .color = COLORS.white };
    const tex = texture.generateCheckerboard();

    for (0..SMALL_ITERS) |_| {
        fb.beginFrame(COLORS.dark);
        fb.drawTriangle(v0, v1, v2, tex[0..]);
    }
}

// ---------------------------------------------------------------------------
// drawTriangle — medium triangle (~150×150 bounding box, ~22.5K px tested)
//
// Approximately the size of a nearby cube face.
// ---------------------------------------------------------------------------

fn benchDrawMedium() void {
    var fb = createBackend();
    defer fb.deinit();

    const v0 = Vertex{ .x = 85, .y = 190, .z = 0, .nx = 0, .ny = 0, .nz = -1, .u = 0, .v = 0, .inv_w = 1, .color = COLORS.white };
    const v1 = Vertex{ .x = 235, .y = 190, .z = 0, .nx = 0, .ny = 0, .nz = -1, .u = 1, .v = 0, .inv_w = 1, .color = COLORS.white };
    const v2 = Vertex{ .x = 160, .y = 40, .z = 0, .nx = 0, .ny = 0, .nz = -1, .u = 0.5, .v = 1, .inv_w = 1, .color = COLORS.white };
    const tex = texture.generateCheckerboard();

    for (0..MEDIUM_ITERS) |_| {
        fb.beginFrame(COLORS.dark);
        fb.drawTriangle(v0, v1, v2, tex[0..]);
    }
}

// ---------------------------------------------------------------------------
// drawTriangle — large triangle (~300×200 bounding box, ~60K px tested)
//
// Approximately half the screen.
// ---------------------------------------------------------------------------

fn benchDrawLarge() void {
    var fb = createBackend();
    defer fb.deinit();

    const v0 = Vertex{ .x = 10, .y = 230, .z = 0, .nx = 0, .ny = 0, .nz = -1, .u = 0, .v = 0, .inv_w = 1, .color = COLORS.white };
    const v1 = Vertex{ .x = 310, .y = 230, .z = 0, .nx = 0, .ny = 0, .nz = -1, .u = 1, .v = 0, .inv_w = 1, .color = COLORS.white };
    const v2 = Vertex{ .x = 160, .y = 10, .z = 0, .nx = 0, .ny = 0, .nz = -1, .u = 0.5, .v = 1, .inv_w = 1, .color = COLORS.white };
    const tex = texture.generateCheckerboard();

    for (0..LARGE_ITERS) |_| {
        fb.beginFrame(COLORS.dark);
        fb.drawTriangle(v0, v1, v2, tex[0..]);
    }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

pub fn run() void {
    std.debug.print("\n  ~ renderer benchmarks ~\n", .{});

    var win = window.windowCreate("infinity-bench", WIDTH, HEIGHT) orelse {
        std.debug.print("  ! window creation failed, skipping renderer benchmarks\n", .{});
        return;
    };
    defer window.windowDestroy(&win);
    bench_window = &win;

    bench("software.beginFrame", benchBeginFrame, 1000);
    bench("software.emptyFrame", benchEmptyFrame, 1000);
    bench("triangle.small (30×30)", benchDrawSmall, SMALL_ITERS);
    bench("triangle.medium (150×150)", benchDrawMedium, MEDIUM_ITERS);
    bench("triangle.large (300×200)", benchDrawLarge, LARGE_ITERS);
}

fn bench(comptime name: []const u8, func: *const fn () void, iters: u32) void {
    const start = time.nanoTime();
    func();
    const elapsed = time.nanoTime() - start;
    const elapsed_f: f64 = @as(f64, @floatFromInt(elapsed));
    const iters_f: f64 = @as(f64, @floatFromInt(iters));
    const ns_per_op = elapsed_f / iters_f;
    std.debug.print("  {s: <30}  {d: >8.1} ns/op\n", .{ name, ns_per_op });
}

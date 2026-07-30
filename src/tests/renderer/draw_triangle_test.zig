//! Golden frame test for drawTriangle.
//!
//! Verifies that the inner-loop safety-off scoping produces output
//! bit-identical to the safety-on reference.
//! Renders a known scene, hashes the framebuffer, compares against a
//! hardcoded golden value.

const std = @import("std");
const testing = std.testing;
const Vertex = @import("../../renderer/renderer.zig").Vertex;
const SoftwareBackend = @import("../../renderer/software.zig").SoftwareBackend;
const Color = @import("../../core/color.zig").Color;
const COLORS = @import("../../core/color.zig").COLORS;
const window = @import("../../platform/window.zig");

const W = 64;
const H = 64;

/// Simple FNV-1a hash over the framebuffer bytes.
fn hashFb(fb: []const u8) u32 {
    var h: u32 = 2166136261;
    for (fb) |byte| {
        h ^= byte;
        h *%= 16777619;
    }
    return h;
}

test "drawTriangle golden frame — two non-overlapping triangles" {
    // Create a test window. Skip gracefully if no display is available.
    var win = window.windowCreate("test-golden", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    var backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    defer backend.deinit();

    const tex = @import("../../renderer/software.zig").generateCheckerboard();

    // --- Frame 1: clear and draw two non-overlapping triangles ---
    backend.beginFrame(COLORS.dark);

    // Triangle 1: top-left quadrant, red-ish, small
    backend.drawTriangle(
        Vertex{ .x = 5, .y = 10, .z = 0.2, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } },
        Vertex{ .x = 25, .y = 10, .z = 0.2, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } },
        Vertex{ .x = 15, .y = 28, .z = 0.2, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } },
        tex[0..],
    );

    // Triangle 2: bottom-right quadrant, different depth, non-overlapping
    backend.drawTriangle(
        Vertex{ .x = 35, .y = 35, .z = 0.6, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } },
        Vertex{ .x = 58, .y = 35, .z = 0.6, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } },
        Vertex{ .x = 46, .y = 55, .z = 0.6, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } },
        tex[0..],
    );

    backend.endFrame();

    // Hash the framebuffer for the golden comparison.
    const h = hashFb(backend.fb);
    // This golden hash was produced with full safety-on debug build.
    // It MUST remain identical after adding @setRuntimeSafety(false)
    // to the inner pixel loop in drawTriangle.
    try testing.expectEqual(@as(u32, 0x170AC603), h);
}

test "T-005 drawTriangle front-to-back Z-order is identical to back-to-front" {
    // Z-buffer is order-independent: drawing near→far should produce
    // identical output to far→near, since the Z-buffer always keeps
    // the nearest fragment. This verifies the front-to-back sort
    // optimization doesn't change visual output.
    var win = window.windowCreate("test-zorder", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    var backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    defer backend.deinit();

    const tex = @import("../../renderer/software.zig").generateCheckerboard();

    // Two overlapping triangles:
    // Tri A (near, z=0.1) — top-left, large, red
    const v0a = Vertex{ .x = 5, .y = 5, .z = 0.1, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } };
    const v1a = Vertex{ .x = 50, .y = 5, .z = 0.1, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } };
    const v2a = Vertex{ .x = 30, .y = 55, .z = 0.1, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } };

    // Tri B (far, z=0.8) — same region, blue, slightly offset
    const v0b = Vertex{ .x = 10, .y = 10, .z = 0.8, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } };
    const v1b = Vertex{ .x = 55, .y = 10, .z = 0.8, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } };
    const v2b = Vertex{ .x = 35, .y = 50, .z = 0.8, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } };

    // Frame 1: far first (unsorted — back-to-front)
    backend.beginFrame(COLORS.dark);
    backend.drawTriangle(v0b, v1b, v2b, tex[0..]); // far drawn first
    backend.drawTriangle(v0a, v1a, v2a, tex[0..]); // near drawn second (overwrites)
    backend.endFrame();
    const hash_backfirst = hashFb(backend.fb);

    // Frame 2: near first (sorted — front-to-back)
    backend.beginFrame(COLORS.dark);
    backend.drawTriangle(v0a, v1a, v2a, tex[0..]); // near drawn first (wins Z-buffer)
    backend.drawTriangle(v0b, v1b, v2b, tex[0..]); // far drawn second (Z-rejected where overlapping)
    backend.endFrame();
    const hash_frontfirst = hashFb(backend.fb);

    // Both orders must produce identical output (Z-buffer is order-independent).
    try testing.expectEqual(hash_backfirst, hash_frontfirst);
}

test "drawTriangle all-pixels-empty after clear" {
    var win = window.windowCreate("test-clear", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    var backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    defer backend.deinit();

    backend.beginFrame(COLORS.dark);

    // No triangles drawn — framebuffer should be all dark.
    const pixel = @as(u32, @bitCast([4]u8{ COLORS.dark.b, COLORS.dark.g, COLORS.dark.r, COLORS.dark.a }));
    const fb32: []u32 = @ptrCast(@alignCast(backend.fb));
    for (fb32) |p| {
        try testing.expectEqual(pixel, p);
    }
}

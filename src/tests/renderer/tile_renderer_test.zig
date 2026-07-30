//! Tile-based rasterizer tests.
//!
//! Verifies tile binning correctness, drawTriangleTile output matches
//! drawTriangle, and full-scene single-threaded vs tile-parallel equivalence.

const std = @import("std");
const testing = std.testing;
const Vertex = @import("../../renderer/renderer.zig").Vertex;
const SoftwareBackend = @import("../../renderer/software.zig").SoftwareBackend;
const ThreadPool = @import("../../core/thread_pool.zig").ThreadPool;
const Color = @import("../../core/color.zig").Color;
const COLORS = @import("../../core/color.zig").COLORS;
const window = @import("../../platform/window.zig");

const W = 64;
const H = 64;
const TILE_SIZE = 32;

/// Simple FNV-1a hash over u32 slice.
fn hashU32(buf: []const u32) u32 {
    var h: u32 = 2166136261;
    for (buf) |word| {
        const b0 = @as(u8, @truncate(word));
        const b1 = @as(u8, @truncate(word >> 8));
        const b2 = @as(u8, @truncate(word >> 16));
        const b3 = @as(u8, @truncate(word >> 24));
        h ^= b0;
        h *%= 16777619;
        h ^= b1;
        h *%= 16777619;
        h ^= b2;
        h *%= 16777619;
        h ^= b3;
        h *%= 16777619;
    }
    return h;
}

/// FNV-1a over u8 slice.
fn hashFb(fb: []const u8) u32 {
    var h: u32 = 2166136261;
    for (fb) |byte| {
        h ^= byte;
        h *%= 16777619;
    }
    return h;
}

// ─── Test 3.3: Tile binning correctness ──────────────────────────────

/// Compute which tiles a triangle with the given screen-space AABB covers.
/// Returns a bitset where bit N is set if tile N covers the AABB.
fn computeTileCoverage(ax: f32, ay: f32, bx: f32, by: f32, cx: f32, cy: f32, num_tiles_x: u32, num_tiles_y: u32) u64 {
    const min_x = @min(@min(ax, bx), cx);
    const min_y = @min(@min(ay, by), cy);
    const max_x = @max(@max(ax, bx), cx);
    const max_y = @max(@max(ay, by), cy);

    if (max_x <= min_x or max_y <= min_y) return 0;
    // Viewport cull: triangle fully off-screen.
    if (max_x <= 0 or max_y <= 0) return 0;

    // Clamp to [0, +inf) — negative coords crash @intFromFloat to u32.
    const c_min_x = @max(0, @as(i32, @intFromFloat(min_x)));
    const c_min_y = @max(0, @as(i32, @intFromFloat(min_y)));
    const c_max_x = @max(0, @as(i32, @intFromFloat(max_x)));
    const c_max_y = @max(0, @as(i32, @intFromFloat(max_y)));

    const tile_x0 = @as(u32, @intCast(c_min_x)) / TILE_SIZE;
    const tile_y0 = @as(u32, @intCast(c_min_y)) / TILE_SIZE;
    const tile_x1 = @min(@as(u32, @intCast(c_max_x)) / TILE_SIZE, num_tiles_x - 1);
    const tile_y1 = @min(@as(u32, @intCast(c_max_y)) / TILE_SIZE, num_tiles_y - 1);

    var bits: u64 = 0;
    var ty: u32 = tile_y0;
    while (ty <= tile_y1) : (ty += 1) {
        var tx: u32 = tile_x0;
        while (tx <= tile_x1) : (tx += 1) {
            const tile_index = ty * num_tiles_x + tx;
            if (tile_index < 64) bits |= (@as(u64, 1) << @intCast(tile_index));
        }
    }
    return bits;
}

test "T-009 tile binning: triangle covering 4 tiles at (0,0) scene corner" {
    // 70x70 triangle at screen position (0,0), TILE_SIZE=32
    // Should cover tiles (0,0), (1,0), (0,1), (1,1)
    const ntx: u32 = W / TILE_SIZE; // 2
    const nty: u32 = H / TILE_SIZE; // 2

    const bits = computeTileCoverage(0, 0, 70, 0, 0, 70, ntx, nty);

    try testing.expect(bits & (1 << 0) != 0); // tile (0,0)
    try testing.expect(bits & (1 << 1) != 0); // tile (1,0)
    try testing.expect(bits & (@as(u64, 1) << @intCast(1 * ntx + 0)) != 0); // tile (0,1) = tile 2
    try testing.expect(bits & (@as(u64, 1) << @intCast(1 * ntx + 1)) != 0); // tile (1,1) = tile 3
    try testing.expectEqual(@as(u32, 4), @popCount(bits));
}

test "T-009b tile binning: degenerate triangle produces zero tiles" {
    // All vertices at same position — zero area AABB
    const bits = computeTileCoverage(50, 50, 50, 50, 50, 50, W / TILE_SIZE, H / TILE_SIZE);
    try testing.expectEqual(@as(u64, 0), bits);
}

test "T-009c tile binning: triangle in single tile" {
    // Small triangle entirely within tile (0,0) — 0..32 x 0..32
    const bits = computeTileCoverage(5, 5, 25, 5, 15, 28, W / TILE_SIZE, H / TILE_SIZE);
    try testing.expectEqual(@as(u64, 1 << 0), bits); // only tile 0
}

test "T-009d tile binning: triangle spanning right edge" {
    // Triangle spanning tiles (0,*) and (1,*) at x=30..40
    const ntx: u32 = W / TILE_SIZE;
    const bits = computeTileCoverage(30, 10, 50, 10, 40, 20, ntx, H / TILE_SIZE);
    // Covers (0,0) and (1,0)
    try testing.expect(bits & (1 << 0) != 0);
    try testing.expect(bits & (1 << 1) != 0);
}

test "T-009e tile binning: triangle partially off-screen does not crash" {
    // Triangle with left half off-screen (coords partially negative).
    // Should cover tile (0,0) only (the visible portion).
    const bits = computeTileCoverage(-10, 5, 15, 5, 5, 20, W / TILE_SIZE, H / TILE_SIZE);
    try testing.expect(bits & (1 << 0) != 0);
}

test "T-009f tile binning: triangle fully off-screen returns zero tiles" {
    // Triangle entirely above the framebuffer.
    const bits = computeTileCoverage(-50, -30, -10, -30, -30, -5, W / TILE_SIZE, H / TILE_SIZE);
    try testing.expectEqual(@as(u64, 0), bits);
}

// ─── Test 3.4: drawTriangleTile vs drawTriangle output match ──────────

test "T-010 drawTriangleTile matches drawTriangle on same triangle (color checksum)" {
    var win = window.windowCreate("test-tile-match", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    var ref_backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    ref_backend.use_simd = false;
    defer ref_backend.deinit();

    const tex = @import("../../renderer/software.zig").generateCheckerboard();
    const tex_slice = tex[0..];

    // Reference: single drawTriangle call covering the whole framebuffer.
    // A triangle large enough to span multiple tiles at known positions.
    const v0 = Vertex{ .x = 5, .y = 50, .z = 0.3, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 100, .b = 50, .a = 255 } };
    const v1 = Vertex{ .x = 58, .y = 50, .z = 0.3, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 100, .b = 50, .a = 255 } };
    const v2 = Vertex{ .x = 32, .y = 5, .z = 0.3, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 200, .g = 100, .b = 50, .a = 255 } };

    ref_backend.beginFrame(COLORS.dark);
    ref_backend.drawTriangle(v0, v1, v2, tex_slice);
    ref_backend.endFrame();
    const ref_hash = hashFb(ref_backend.fb);

    // Tile-based: render same triangle through drawTriangleTile per tile.
    var tile_zb: [TILE_SIZE * TILE_SIZE]f32 = undefined;
    var tile_cb: [TILE_SIZE * TILE_SIZE]u32 = undefined;

    var tile_backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    tile_backend.use_simd = false;
    defer tile_backend.deinit();

    tile_backend.beginFrame(COLORS.dark);

    const num_tiles_x = W / TILE_SIZE; // 2
    const num_tiles_y = H / TILE_SIZE; // 2

    var ty: u32 = 0;
    while (ty < num_tiles_y) : (ty += 1) {
        var tx: u32 = 0;
        while (tx < num_tiles_x) : (tx += 1) {
            const bg_pixel = @as(u32, @bitCast([4]u8{ COLORS.dark.b, COLORS.dark.g, COLORS.dark.r, COLORS.dark.a }));
            @memset(&tile_zb, 1.0);
            @memset(&tile_cb, bg_pixel);

            tile_backend.drawTriangleTile(v0, v1, v2, tex_slice,
                tx * TILE_SIZE, ty * TILE_SIZE, &tile_zb, &tile_cb);

            // Copy tile to framebuffer
            const pitch = W * 4;
            for (0..TILE_SIZE) |row| {
                const y_abs = ty * TILE_SIZE + row;
                if (y_abs >= H) break;
                const src_off = row * TILE_SIZE;
                const dst_off = y_abs * pitch + tx * TILE_SIZE * 4;
                const src_bytes = std.mem.sliceAsBytes(tile_cb[src_off..src_off + TILE_SIZE]);
                @memcpy(tile_backend.fb[dst_off..dst_off + src_bytes.len], src_bytes);
            }
        }
    }

    tile_backend.endFrame();
    const tile_hash = hashFb(tile_backend.fb);

    // Both must produce identical framebuffer output.
    try testing.expectEqual(ref_hash, tile_hash);
}

test "T-010b drawTriangleTile empty tile skips correctly" {
    var win = window.windowCreate("test-tile-empty", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    var backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    backend.use_simd = false;
    defer backend.deinit();

    const tex = @import("../../renderer/software.zig").generateCheckerboard();

    // Triangle completely outside the framebuffer
    const v0 = Vertex{ .x = -100, .y = -100, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 255, .g = 255, .b = 255, .a = 255 } };
    const v1 = Vertex{ .x = -50, .y = -100, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 255, .g = 255, .b = 255, .a = 255 } };
    const v2 = Vertex{ .x = -75, .y = -50, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 255, .g = 255, .b = 255, .a = 255 } };

    const bg_pixel = @as(u32, @bitCast([4]u8{ COLORS.dark.b, COLORS.dark.g, COLORS.dark.r, COLORS.dark.a }));
    var tile_zb: [TILE_SIZE * TILE_SIZE]f32 = undefined;
    var tile_cb: [TILE_SIZE * TILE_SIZE]u32 = undefined;
    @memset(&tile_zb, 1.0);
    @memset(&tile_cb, bg_pixel);

    // Render into tile (0,0)
    backend.drawTriangleTile(v0, v1, v2, tex[0..], 0, 0, &tile_zb, &tile_cb);

    // Tile should be all background (no pixels touched)
    for (tile_cb) |p| {
        try testing.expectEqual(bg_pixel, p);
    }
}

// ─── Test 3.5: Full scene single-threaded vs tile-parallel ─────────

test "T-011 full scene tile-parallel matches single-threaded (framebuffer checksum)" {
    var win = window.windowCreate("test-tile-parallel", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    var single_backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    single_backend.use_simd = false;
    defer single_backend.deinit();

    var tile_backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    tile_backend.use_simd = false;
    defer tile_backend.deinit();

    var pool = try ThreadPool.init(testing.allocator, 4);
    defer pool.deinit();

    const tex = @import("../../renderer/software.zig").generateCheckerboard();
    const tex_slice = tex[0..];

    // 4 triangles forming a small scene (non-overlapping depth order).
    const Triple = struct { v0: Vertex, v1: Vertex, v2: Vertex };
    const tris = [_]Triple{
        // Back-left triangle (far, z=0.8)
        .{ .v0 = Vertex{ .x = 5, .y = 5, .z = 0.8, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } },
           .v1 = Vertex{ .x = 30, .y = 5, .z = 0.8, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } },
           .v2 = Vertex{ .x = 18, .y = 30, .z = 0.8, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 200, .g = 50, .b = 50, .a = 255 } } },
        // Front-right triangle (near, z=0.2)
        .{ .v0 = Vertex{ .x = 35, .y = 35, .z = 0.2, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } },
           .v1 = Vertex{ .x = 60, .y = 35, .z = 0.2, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } },
           .v2 = Vertex{ .x = 48, .y = 58, .z = 0.2, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 50, .g = 50, .b = 200, .a = 255 } } },
        // Top-middle triangle (mid, z=0.5)
        .{ .v0 = Vertex{ .x = 20, .y = 40, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 200, .b = 50, .a = 255 } },
           .v1 = Vertex{ .x = 45, .y = 40, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 50, .g = 200, .b = 50, .a = 255 } },
           .v2 = Vertex{ .x = 32, .y = 18, .z = 0.5, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 50, .g = 200, .b = 50, .a = 255 } } },
        // Bottom edge triangle (z=0.6)
        .{ .v0 = Vertex{ .x = 10, .y = 55, .z = 0.6, .nx = 0, .ny = 0, .nz = 0, .u = 0, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 200, .b = 50, .a = 255 } },
           .v1 = Vertex{ .x = 55, .y = 55, .z = 0.6, .nx = 0, .ny = 0, .nz = 0, .u = 1, .v = 0, .inv_w = 1.0, .color = Color{ .r = 200, .g = 200, .b = 50, .a = 255 } },
           .v2 = Vertex{ .x = 32, .y = 62, .z = 0.6, .nx = 0, .ny = 0, .nz = 0, .u = 0.5, .v = 1, .inv_w = 1.0, .color = Color{ .r = 200, .g = 200, .b = 50, .a = 255 } } },
    };

    // Single-threaded reference.
    single_backend.beginFrame(COLORS.dark);
    for (&tris) |tri| {
        single_backend.drawTriangle(tri.v0, tri.v1, tri.v2, tex_slice);
    }
    single_backend.endFrame();
    const ref_hash = hashFb(single_backend.fb);

    // Tile-parallel: render through a tile-based dispatcher with 4 workers.
    {
        tile_backend.beginFrame(COLORS.dark);

        const num_tiles_x = W / TILE_SIZE;
        const num_tiles_y = H / TILE_SIZE;
        // Bin triangles to tiles (inline tile binning for test).
        const MAX_TRIS_PER_TILE = 64;
        var tile_tris: [4 * MAX_TRIS_PER_TILE]u32 = undefined;
        var tile_counts: [4]u32 = .{0} ** 4;

        for (&tris, 0..) |tri, tri_idx| {
            const min_x_f = @min(@min(tri.v0.x, tri.v1.x), tri.v2.x);
            const min_y_f = @min(@min(tri.v0.y, tri.v1.y), tri.v2.y);
            const max_x_f = @max(@max(tri.v0.x, tri.v1.x), tri.v2.x);
            const max_y_f = @max(@max(tri.v0.y, tri.v1.y), tri.v2.y);

            if (max_x_f <= min_x_f or max_y_f <= min_y_f) continue;

            const tile_x0 = @as(u32, @intFromFloat(min_x_f)) / TILE_SIZE;
            const tile_y0 = @as(u32, @intFromFloat(min_y_f)) / TILE_SIZE;
            const tile_x1 = @min(@as(u32, @intFromFloat(max_x_f)) / TILE_SIZE, num_tiles_x - 1);
            const tile_y1 = @min(@as(u32, @intFromFloat(max_y_f)) / TILE_SIZE, num_tiles_y - 1);

            var ty: u32 = tile_y0;
            while (ty <= tile_y1) : (ty += 1) {
                var tx: u32 = tile_x0;
                while (tx <= tile_x1) : (tx += 1) {
                    const tile_i = ty * num_tiles_x + tx;
                    const off = tile_counts[tile_i];
                    if (off < MAX_TRIS_PER_TILE) {
                        tile_tris[tile_i * MAX_TRIS_PER_TILE + off] = @intCast(tri_idx);
                        tile_counts[tile_i] = off + 1;
                    }
                }
            }
        }

        // Dispatch tiles to workers.
        const TileTestCtx = struct {
            backend: *SoftwareBackend,
            tris: []const Triple,
            tex: []const u8,
            tile_tris: []const u32,
            tile_counts: []const u32,
            num_tiles_x: u32,
            num_tiles_y: u32,
            tile_next: std.atomic.Value(u32),
        };

        const tile_next = std.atomic.Value(u32).init(0);
        var test_ctx = TileTestCtx{
            .backend = &tile_backend,
            .tris = &tris,
            .tex = tex_slice,
            .tile_tris = &tile_tris,
            .tile_counts = &tile_counts,
            .num_tiles_x = num_tiles_x,
            .num_tiles_y = num_tiles_y,
            .tile_next = tile_next,
        };

        const TileWorker = struct {
            fn tileWorkerTest(idx: usize, ctx: *TileTestCtx) void {
                _ = idx;
                while (true) {
                    const tile_i = ctx.tile_next.fetchAdd(1, .monotonic);
                    if (tile_i >= ctx.num_tiles_x * ctx.num_tiles_y) break;

                    const tx = tile_i % ctx.num_tiles_x;
                    const ty = tile_i / ctx.num_tiles_x;
                    const count = ctx.tile_counts[tile_i];

                    if (count == 0) continue;

                    const bg_pixel = @as(u32, @bitCast([4]u8{ COLORS.dark.b, COLORS.dark.g, COLORS.dark.r, COLORS.dark.a }));
                    var tile_zb: [TILE_SIZE * TILE_SIZE]f32 = undefined;
                    var tile_cb: [TILE_SIZE * TILE_SIZE]u32 = undefined;
                    @memset(&tile_zb, 1.0);
                    @memset(&tile_cb, bg_pixel);

                    const start = tile_i * MAX_TRIS_PER_TILE;
                    for (0..count) |t| {
                        const tri_idx = ctx.tile_tris[start + t];
                        const tri = ctx.tris[tri_idx];
                        ctx.backend.*.drawTriangleTile(tri.v0, tri.v1, tri.v2, ctx.tex,
                            tx * TILE_SIZE, ty * TILE_SIZE, &tile_zb, &tile_cb);
                    }

                    // Copy tile to framebuffer.
                    const pitch: u32 = W * 4;
                    for (0..TILE_SIZE) |row| {
                        const y_abs = ty * TILE_SIZE + row;
                        if (y_abs >= H) break;
                        const src_off = row * TILE_SIZE;
                        const dst_off = y_abs * pitch + tx * TILE_SIZE * 4;
                        const src_bytes = std.mem.sliceAsBytes(tile_cb[src_off..src_off + TILE_SIZE]);
                        @memcpy(ctx.backend.fb[dst_off..dst_off + src_bytes.len], src_bytes);
                    }
                }
            }
        }.tileWorkerTest;

        pool.spawn(TileWorker, &test_ctx, 4);
        pool.wait();

        tile_backend.endFrame();
        const tile_hash = hashFb(tile_backend.fb);

        try testing.expectEqual(ref_hash, tile_hash);
    }
}

// ─── RED tests for Task 2.2-2.5 implementation ─────────────────────

test "R-2.2 copyTileToFb copies tile data to correct framebuffer offset" {
    // RED: copyTileToFb does not exist yet on SoftwareBackend
    var win = window.windowCreate("test-copy-tile", W, H) orelse return error.SkipZigTest;
    defer window.windowDestroy(&win);

    var backend = try SoftwareBackend.init(testing.allocator, &win, W, H);
    backend.use_simd = false;
    defer backend.deinit();

    // Fill a small tile color buffer with a known checker pattern
    var tile_cb: [TILE_SIZE * TILE_SIZE]u32 = undefined;
    for (0..TILE_SIZE) |row| {
        for (0..TILE_SIZE) |col| {
            const val: u32 = @intCast((row * TILE_SIZE + col) * 13);
            tile_cb[row * TILE_SIZE + col] = @bitCast([4]u8{
                @intCast(val & 0xFF),
                @intCast((val >> 4) & 0xFF),
                @intCast((val >> 8) & 0xFF),
                255,
            });
        }
    }

    // Clear framebuffer to known color
    backend.beginFrame(COLORS.dark);

    // This call should not exist yet → COMPILE ERROR (RED)
    backend.copyTileToFb(0, 0, &tile_cb, W, H);

    // Verify first pixel of tile was written
    const fb_u32: []u32 = @ptrCast(@alignCast(backend.fb));
    try testing.expect(fb_u32[0] != 0);
}



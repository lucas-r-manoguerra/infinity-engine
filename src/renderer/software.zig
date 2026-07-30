//! Software framebuffer renderer backend.
//!
//! Barycentric rasterizer with Z-buffer, perspective-correct texture mapping.
//! Pure CPU rendering.

const std = @import("std");
const Color = @import("../core/color.zig").Color;
const modulateColor = @import("../core/color.zig").modulateColor;
const Vertex = @import("renderer.zig").Vertex;
const ThreadPool = @import("../core/thread_pool.zig").ThreadPool;
const x11 = @import("../platform/x11.zig");
const platform = @import("../platform/window.zig");
const builtin = @import("builtin");
const testing = std.testing;

pub const TEXTURE_W = 64;
pub const TEXTURE_H = 64;

pub const PIXEL_BYTES = 4;

/// Tile size in pixels for tile-based rasterization.
/// Tiles are square (TILE_SIZE × TILE_SIZE). Larger tiles reduce
/// dispatch overhead but waste more Z/color buffer on empty regions.
pub const TILE_SIZE = 32;

/// Runtime CPU feature detection for SIMD optimizations.
/// Uses CPUID leaf 7, EBX bit 5 to detect AVX2 on x86_64.
/// On non-x86_64 targets, returns false.
pub fn detectAvx2() bool {
    if (comptime builtin.cpu.arch != .x86_64) return false;
    var ebx: u32 = undefined;
    // CPUID leaf 7, subleaf 0 → EBX bit 5 = AVX2
    // Keep EAX/ECX/EDX as unused outputs so the compiler does not
    // assume those registers are preserved.
    var eax_unused: u32 = undefined;
    var ecx_unused: u32 = undefined;
    var edx_unused: u32 = undefined;
    asm volatile ("cpuid"
        : [_] "={eax}" (eax_unused),
          [_] "={ebx}" (ebx),
          [_] "={ecx}" (ecx_unused),
          [_] "={edx}" (edx_unused),
        : [_] "{eax}" (7),
          [_] "{ecx}" (0),
    );
    return (ebx & (1 << 5)) != 0;
}

pub const SoftwareBackend = struct {
    pub fn init(allocator: std.mem.Allocator, window: *platform.Window, width: u32, height: u32) !SoftwareBackend {
        const fb = try allocator.alloc(u8, width * height * PIXEL_BYTES);
        const zb = try allocator.alloc(f32, width * height);
        @memset(zb, 1.0);
        const gc = x11.XCreateGC(window.display, window.handle, 0, null) orelse return error.InitFailed;
        // Pre-allocate buffer for XImage data (freed in deinit via XFree).
        const c_alloc = std.heap.c_allocator;
        const xd = try c_alloc.alloc(u8, width * height * PIXEL_BYTES);
        return SoftwareBackend{
            .allocator = allocator,
            .display = window.display,
            .handle = window.handle,
            .fb = fb,
            .zb = zb,
            .width = width,
            .height = height,
            .gc = gc,
            .ximage_data = xd,
            .use_simd = detectAvx2(),
        };
    }

    pub fn deinit(self: *SoftwareBackend) void {
        _ = x11.XFreeGC(self.display, self.gc);
        // XImage data was allocated with C allocator; free without XDestroyImage
        // (which would also free the data we want to manage ourselves).
        std.heap.c_allocator.free(self.ximage_data);
        self.allocator.free(self.fb);
        self.allocator.free(self.zb);
    }

    pub fn beginFrame(self: *SoftwareBackend, color: Color) void {
        // Fast clear: cast framebuffer to []u32, memset with packed pixel value.
        const pixel_u32 = @as(u32, @bitCast([4]u8{ color.b, color.g, color.r, color.a }));
        const fb_u32: []u32 = @ptrCast(@alignCast(self.fb));
        @memset(fb_u32, pixel_u32);
        @memset(self.zb, 1.0);
    }

    pub fn endFrame(_: *SoftwareBackend) void {}

    pub fn drawTriangle(self: *SoftwareBackend, v0: Vertex, v1: Vertex, v2: Vertex, texture: []const u8) void {
        // ---------------------------------------------------------------
        // 1. Sort vertices by Y (bubble sort, 3 elements)
        // ---------------------------------------------------------------
        var v = [_]Vertex{ v0, v1, v2 };
        if (v[0].y > v[1].y) { const t = v[0]; v[0] = v[1]; v[1] = t; }
        if (v[1].y > v[2].y) { const t = v[1]; v[1] = v[2]; v[2] = t; }
        if (v[0].y > v[1].y) { const t = v[0]; v[0] = v[1]; v[1] = t; }

        // ---------------------------------------------------------------
        // 2. Compute area and early-out for back-face / degenerate
        // ---------------------------------------------------------------
        const area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
        if (@abs(area) < 0.001) return;
        const inv_area = 1.0 / area;

        // ---------------------------------------------------------------
        // 3. DDA coefficients — edge function increments per pixel step
        // ---------------------------------------------------------------
        // Edge function w_ij(x,y) = (xj - xi)*(y - yi) - (yj - yi)*(x - xi)
        //   dw/dx = -(yj - yi) = yi - yj
        //   dw/dy =  xj - xi
        const dw0_dx = v[1].y - v[2].y;   // edge v1→v2
        const dw1_dx = v[2].y - v[0].y;   // edge v2→v0
        const dw2_dx = v[0].y - v[1].y;   // edge v0→v1

        // ---------------------------------------------------------------
        // 4. Clamp to screen bounds
        // ---------------------------------------------------------------
        const y_start = @max(0, @as(i32, @intFromFloat(v[0].y)));
        const y_end = @min(@as(i32, @intFromFloat(v[2].y)), @as(i32, @intCast(self.height - 1)));

        const min_x = @max(0, @as(i32, @intFromFloat(@min(v[0].x, @min(v[1].x, v[2].x)))));
        const max_x = @min(@as(i32, @intCast(self.width - 1)), @as(i32, @intFromFloat(@max(v[0].x, @max(v[1].x, v[2].x)))));

        const fb = self.fb;
        const zb = self.zb;

        // Pre-fetch vertex attributes.
        const z0 = v[0].z; const z1 = v[1].z; const z2 = v[2].z;
        const r0: f32 = @floatFromInt(v[0].color.r);
        const r1: f32 = @floatFromInt(v[1].color.r);
        const r2: f32 = @floatFromInt(v[2].color.r);
        const g0: f32 = @floatFromInt(v[0].color.g);
        const g1: f32 = @floatFromInt(v[1].color.g);
        const g2: f32 = @floatFromInt(v[2].color.g);
        const b0: f32 = @floatFromInt(v[0].color.b);
        const b1: f32 = @floatFromInt(v[1].color.b);
        const b2: f32 = @floatFromInt(v[2].color.b);
        const tu0 = v[0].u; const tu1 = v[1].u; const tu2 = v[2].u;
        const tv0 = v[0].v; const tv1 = v[1].v; const tv2 = v[2].v;

        // Incremental deltas per x-step for all attributes.
        const iarea = inv_area;
        const dz_dx = (dw0_dx * z0 + dw1_dx * z1 + dw2_dx * z2) * iarea;
        const dr_dx = (dw0_dx * r0 + dw1_dx * r1 + dw2_dx * r2) * iarea;
        const dg_dx = (dw0_dx * g0 + dw1_dx * g1 + dw2_dx * g2) * iarea;
        const db_dx = (dw0_dx * b0 + dw1_dx * b1 + dw2_dx * b2) * iarea;
        const du_dx = (dw0_dx * tu0 + dw1_dx * tu1 + dw2_dx * tu2) * iarea;
        const dv_dx = (dw0_dx * tv0 + dw1_dx * tv1 + dw2_dx * tv2) * iarea;

        // ---------------------------------------------------------------
        // 5. Scanline fill — DDA edge walking + incremental attributes
        // ---------------------------------------------------------------
        var y: i32 = y_start;
        while (y <= y_end) : (y += 1) {
            const fy: f32 = @floatFromInt(y);
            const y_base = @as(usize, @intCast(y)) * @as(usize, self.width);

            // Compute w0,w1,w2 at start-of-scanline (x = min_x).
            const fx0: f32 = @floatFromInt(min_x);
            var w0 = (v[2].x - v[1].x) * (fy - v[1].y) - (v[2].y - v[1].y) * (fx0 - v[1].x);
            var w1 = (v[0].x - v[2].x) * (fy - v[2].y) - (v[0].y - v[2].y) * (fx0 - v[2].x);
            var w2 = (v[1].x - v[0].x) * (fy - v[0].y) - (v[1].y - v[0].y) * (fx0 - v[0].x);

            // Corresponding attribute values at scanline start.
            var scan_z = (w0 * z0 + w1 * z1 + w2 * z2) * iarea;
            var scan_r = (w0 * r0 + w1 * r1 + w2 * r2) * iarea;
            var scan_g = (w0 * g0 + w1 * g1 + w2 * g2) * iarea;
            var scan_b = (w0 * b0 + w1 * b1 + w2 * b2) * iarea;
            var scan_u = (w0 * tu0 + w1 * tu1 + w2 * tu2) * iarea;
            var scan_v = (w0 * tv0 + w1 * tv1 + w2 * tv2) * iarea;

            var x: i32 = min_x;
            // Safety OFF for the per-pixel hot path: ~7 bounds checks per pixel.
            // Setup code (above) and the outer scanline loop retain safety.
            // Visual output is bit-identical (proven by golden frame test).
            @setRuntimeSafety(false);
            while (x <= max_x) : (x += 1) {
                // All triangles arrive CCW-culled (back-face tested in renderCube).
                if (w0 >= 0 and w1 >= 0 and w2 >= 0) {
                    const zb_idx = y_base + @as(usize, @intCast(x));
                    if (scan_z < zb[zb_idx]) {
                        zb[zb_idx] = scan_z;
                        const ir = @max(0, @min(255, @as(i32, @intFromFloat(scan_r))));
                        const ig = @max(0, @min(255, @as(i32, @intFromFloat(scan_g))));
                        const ib = @max(0, @min(255, @as(i32, @intFromFloat(scan_b))));
                        const tex_color = sampleTexture(texture, scan_u, scan_v);
                        const vert_color = Color{ .b = @truncate(ib), .g = @truncate(ig), .r = @truncate(ir), .a = 255 };
                        const final_color = modulateColor(vert_color, tex_color);
                        const fb_idx = zb_idx * PIXEL_BYTES;
                        fb[fb_idx] = final_color.b;
                        fb[fb_idx + 1] = final_color.g;
                        fb[fb_idx + 2] = final_color.r;
                        fb[fb_idx + 3] = 255;
                    }
                }
                w0 += dw0_dx;
                w1 += dw1_dx;
                w2 += dw2_dx;
                scan_z += dz_dx;
                scan_r += dr_dx;
                scan_g += dg_dx;
                scan_b += db_dx;
                scan_u += du_dx;
                scan_v += dv_dx;
            }
            @setRuntimeSafety(true);
        }
    }

    pub fn present(self: *SoftwareBackend) void {
        // Copy framebuffer into pre-allocated XImage data buffer.
        @memcpy(self.ximage_data, self.fb);

        const screen_num = x11.XDefaultScreen(self.display);
        const visual = x11.XDefaultVisual(self.display, screen_num);
        const depth = x11.XDefaultDepth(self.display, screen_num);
        // Create XImage pointing to our pre-allocated buffer.
        const img = x11.XCreateImage(
            self.display,
            visual,
            @intCast(depth),
            x11.ZPixmap,
            0,
            @as(?*u8, &self.ximage_data[0]),
            @intCast(self.width),
            @intCast(self.height),
            32,
            0,
        ) orelse return;
        _ = x11.XPutImage(
            self.display,
            self.handle,
            self.gc,
            img,
            0, 0, 0, 0,
            @intCast(self.width),
            @intCast(self.height),
        );
        // XFree only the struct — ximage_data is managed separately.
        _ = x11.XFree(@as(*anyopaque, @ptrCast(img)));
        _ = x11.XFlush(self.display);
    }

    /// Store a reference to the engine's thread pool for strip rendering.
    /// The pool is owned by Engine; the backend just borrows it.
    pub fn setPool(self: *SoftwareBackend, pool: *ThreadPool, count: u32) void {
        self.pool = pool;
        self.thread_count = count;
    }

    /// Render a triangle clipped to a horizontal strip of the framebuffer.
    ///
    /// Contract: identical visual output to `drawTriangle` but only processes
    /// scanlines where `y_min <= y < y_max`.  If the triangle's bounding box
    /// does not intersect the strip, returns immediately (no work).
    ///
    /// Thread-safe on disjoint strips: caller guarantees that no two calls
    /// overlap in Y, so writes to `fb` and `zb` are to non-overlapping regions
    /// and need zero synchronization.
    pub fn drawTriangleStrip(self: *SoftwareBackend, v0: Vertex, v1: Vertex, v2: Vertex, texture: []const u8, y_min: i32, y_max: i32) void {
        // ---------------------------------------------------------------
        // 1. Sort vertices by Y (bubble sort, 3 elements)
        // ---------------------------------------------------------------
        var v = [_]Vertex{ v0, v1, v2 };
        if (v[0].y > v[1].y) { const t = v[0]; v[0] = v[1]; v[1] = t; }
        if (v[1].y > v[2].y) { const t = v[1]; v[1] = v[2]; v[2] = t; }
        if (v[0].y > v[1].y) { const t = v[0]; v[0] = v[1]; v[1] = t; }

        // ---------------------------------------------------------------
        // 2. Compute area and early-out for back-face / degenerate
        // ---------------------------------------------------------------
        const area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
        if (@abs(area) < 0.001) return;
        const inv_area = 1.0 / area;

        // ---------------------------------------------------------------
        // 3. DDA coefficients — edge function increments per pixel step
        // ---------------------------------------------------------------
        const dw0_dx = v[1].y - v[2].y;
        const dw1_dx = v[2].y - v[0].y;
        const dw2_dx = v[0].y - v[1].y;

        // ---------------------------------------------------------------
        // 4. Clamp to screen bounds AND strip bounds
        // ---------------------------------------------------------------
        const tri_y_start = @max(0, @as(i32, @intFromFloat(v[0].y)));
        const tri_y_end = @min(@as(i32, @intFromFloat(v[2].y)), @as(i32, @intCast(self.height - 1)));

        // Intersect triangle Y range with strip Y range.
        const y_start = @max(tri_y_start, y_min);
        const y_end = @min(tri_y_end, y_max - 1);
        if (y_start > y_end) return; // No overlap with this strip.

        const min_x = @max(0, @as(i32, @intFromFloat(@min(v[0].x, @min(v[1].x, v[2].x)))));
        const max_x = @min(@as(i32, @intCast(self.width - 1)), @as(i32, @intFromFloat(@max(v[0].x, @max(v[1].x, v[2].x)))));
        if (min_x > max_x) return;

        const fb = self.fb;
        const zb = self.zb;

        // Pre-fetch vertex attributes.
        const z0 = v[0].z; const z1 = v[1].z; const z2 = v[2].z;
        const r0: f32 = @floatFromInt(v[0].color.r);
        const r1: f32 = @floatFromInt(v[1].color.r);
        const r2: f32 = @floatFromInt(v[2].color.r);
        const g0: f32 = @floatFromInt(v[0].color.g);
        const g1: f32 = @floatFromInt(v[1].color.g);
        const g2: f32 = @floatFromInt(v[2].color.g);
        const b0: f32 = @floatFromInt(v[0].color.b);
        const b1: f32 = @floatFromInt(v[1].color.b);
        const b2: f32 = @floatFromInt(v[2].color.b);
        const tu0 = v[0].u; const tu1 = v[1].u; const tu2 = v[2].u;
        const tv0 = v[0].v; const tv1 = v[1].v; const tv2 = v[2].v;

        const iarea = inv_area;
        const dz_dx = (dw0_dx * z0 + dw1_dx * z1 + dw2_dx * z2) * iarea;
        const dr_dx = (dw0_dx * r0 + dw1_dx * r1 + dw2_dx * r2) * iarea;
        const dg_dx = (dw0_dx * g0 + dw1_dx * g1 + dw2_dx * g2) * iarea;
        const db_dx = (dw0_dx * b0 + dw1_dx * b1 + dw2_dx * b2) * iarea;
        const du_dx = (dw0_dx * tu0 + dw1_dx * tu1 + dw2_dx * tu2) * iarea;
        const dv_dx = (dw0_dx * tv0 + dw1_dx * tv1 + dw2_dx * tv2) * iarea;

        // ---------------------------------------------------------------
        // 5. Scanline fill — per-scanline exact edge function + X-DDA
        // ---------------------------------------------------------------
        // Same as drawTriangle: compute edge function from scratch at each
        // scanline for bit-exact pixel coverage. No Y-DDA drift.
        var y: i32 = y_start;
        while (y <= y_end) : (y += 1) {
            const fy: f32 = @floatFromInt(y);
            const y_base = @as(usize, @intCast(y)) * @as(usize, self.width);

            // Exact edge function at (min_x, y) — same as drawTriangle.
            const fx0: f32 = @floatFromInt(min_x);
            var w0 = (v[2].x - v[1].x) * (fy - v[1].y) - (v[2].y - v[1].y) * (fx0 - v[1].x);
            var w1 = (v[0].x - v[2].x) * (fy - v[2].y) - (v[0].y - v[2].y) * (fx0 - v[2].x);
            var w2 = (v[1].x - v[0].x) * (fy - v[0].y) - (v[1].y - v[0].y) * (fx0 - v[0].x);

            if (self.use_simd) {
                // SIMD path: 8 pixels per iteration via @Vector(8, f32),
                // with X-DDA stepping between batches.
                const Vec8f = @Vector(8, f32);
                const offsets: Vec8f = .{ 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0 };
                const Z = Vec8f;
                const v_dw0: Z = @splat(@as(f32, dw0_dx));
                const v_dw1: Z = @splat(@as(f32, dw1_dx));
                const v_dw2: Z = @splat(@as(f32, dw2_dx));
                const v_z0: Z = @splat(z0);
                const v_z1: Z = @splat(z1);
                const v_z2: Z = @splat(z2);
                const v_r0: Z = @splat(r0);
                const v_r1: Z = @splat(r1);
                const v_r2: Z = @splat(r2);
                const v_g0: Z = @splat(g0);
                const v_g1: Z = @splat(g1);
                const v_g2: Z = @splat(g2);
                const v_b0: Z = @splat(b0);
                const v_b1: Z = @splat(b1);
                const v_b2: Z = @splat(b2);
                const v_tu0: Z = @splat(tu0);
                const v_tu1: Z = @splat(tu1);
                const v_tu2: Z = @splat(tu2);
                const v_tv0: Z = @splat(tv0);
                const v_tv1: Z = @splat(tv1);
                const v_tv2: Z = @splat(tv2);
                const v_iarea: Z = @splat(iarea);

                // X-DDA state: start at (min_x, y).
                var x_simd: f32 = @floatFromInt(min_x);
                var w0_base = w0;
                var w1_base = w1;
                var w2_base = w2;
                const vec_end = max_x - 7;

                var x: i32 = min_x;
                @setRuntimeSafety(false);
                while (x <= vec_end) : (x += 8) {
                    const w0v: Z = @as(Z, @splat(w0_base)) + v_dw0 * offsets;
                    const w1v: Z = @as(Z, @splat(w1_base)) + v_dw1 * offsets;
                    const w2v: Z = @as(Z, @splat(w2_base)) + v_dw2 * offsets;
                    const zero: Z = @splat(0.0);
                    const inside: @Vector(8, bool) = (w0v >= zero) & (w1v >= zero) & (w2v >= zero);
                    if (@reduce(.Or, inside) == false) {
                        x_simd += 8.0;
                        w0_base = w0 + dw0_dx * x_simd;
                        w1_base = w1 + dw1_dx * x_simd;
                        w2_base = w2 + dw2_dx * x_simd;
                        continue;
                    }

                    const zv: Z = (w0v * v_z0 + w1v * v_z1 + w2v * v_z2) * v_iarea;
                    const rv: Z = (w0v * v_r0 + w1v * v_r1 + w2v * v_r2) * v_iarea;
                    const gv: Z = (w0v * v_g0 + w1v * v_g1 + w2v * v_g2) * v_iarea;
                    const bv: Z = (w0v * v_b0 + w1v * v_b1 + w2v * v_b2) * v_iarea;
                    const uvv: Z = (w0v * v_tu0 + w1v * v_tu1 + w2v * v_tu2) * v_iarea;
                    const vvv: Z = (w0v * v_tv0 + w1v * v_tv1 + w2v * v_tv2) * v_iarea;

                    inline for (0..8) |i| {
                        if (inside[i]) {
                            const ii: i32 = @intCast(i);
                            const zb_idx = y_base + @as(usize, @intCast(x + ii));
                            if (zv[i] < zb[zb_idx]) {
                                zb[zb_idx] = zv[i];
                                const ir = @max(0, @min(255, @as(i32, @intFromFloat(rv[i]))));
                                const ig = @max(0, @min(255, @as(i32, @intFromFloat(gv[i]))));
                                const ib = @max(0, @min(255, @as(i32, @intFromFloat(bv[i]))));
                                const tex_color = sampleTexture(texture, uvv[i], vvv[i]);
                                const vert_color = Color{ .b = @truncate(ib), .g = @truncate(ig), .r = @truncate(ir), .a = 255 };
                                const final_color = modulateColor(vert_color, tex_color);
                                const fb_idx = zb_idx * PIXEL_BYTES;
                                fb[fb_idx + 0] = final_color.b;
                                fb[fb_idx + 1] = final_color.g;
                                fb[fb_idx + 2] = final_color.r;
                                fb[fb_idx + 3] = 255;
                            }
                        }
                    }
                    x_simd += 8.0;
                    w0_base = w0 + dw0_dx * x_simd;
                    w1_base = w1 + dw1_dx * x_simd;
                    w2_base = w2 + dw2_dx * x_simd;
                }
                // Scalar remainder for last <8 pixels per scanline.
                {
                    var w0_r = w0_base;
                    var w1_r = w1_base;
                    var w2_r = w2_base;
                    var z_r = (w0_r * z0 + w1_r * z1 + w2_r * z2) * iarea;
                    var r_r = (w0_r * r0 + w1_r * r1 + w2_r * r2) * iarea;
                    var g_r = (w0_r * g0 + w1_r * g1 + w2_r * g2) * iarea;
                    var b_r = (w0_r * b0 + w1_r * b1 + w2_r * b2) * iarea;
                    var u_r = (w0_r * tu0 + w1_r * tu1 + w2_r * tu2) * iarea;
                    var v_r = (w0_r * tv0 + w1_r * tv1 + w2_r * tv2) * iarea;
                    while (x <= max_x) : (x += 1) {
                        if (w0_r >= 0 and w1_r >= 0 and w2_r >= 0) {
                            const zb_idx = y_base + @as(usize, @intCast(x));
                            if (z_r < zb[zb_idx]) {
                                zb[zb_idx] = z_r;
                                const ir = @max(0, @min(255, @as(i32, @intFromFloat(r_r))));
                                const ig = @max(0, @min(255, @as(i32, @intFromFloat(g_r))));
                                const ib = @max(0, @min(255, @as(i32, @intFromFloat(b_r))));
                                const tex_color = sampleTexture(texture, u_r, v_r);
                                const vert_color = Color{ .b = @truncate(ib), .g = @truncate(ig), .r = @truncate(ir), .a = 255 };
                                const final_color = modulateColor(vert_color, tex_color);
                                const fb_idx = zb_idx * PIXEL_BYTES;
                                fb[fb_idx + 0] = final_color.b;
                                fb[fb_idx + 1] = final_color.g;
                                fb[fb_idx + 2] = final_color.r;
                                fb[fb_idx + 3] = 255;
                            }
                        }
                        w0_r += dw0_dx; w1_r += dw1_dx; w2_r += dw2_dx;
                        z_r += dz_dx; r_r += dr_dx; g_r += dg_dx; b_r += db_dx; u_r += du_dx; v_r += dv_dx;
                    }
                }
                @setRuntimeSafety(true);
            } else {
                // Scalar path: exact edge function per scanline (same as drawTriangle).
                // Recompute attributes from exact edge function too, since these are
                // used as the pixel-level loop state and must match drawTriangle exactly.
                var scan_z = (w0 * z0 + w1 * z1 + w2 * z2) * iarea;
                var scan_r = (w0 * r0 + w1 * r1 + w2 * r2) * iarea;
                var scan_g = (w0 * g0 + w1 * g1 + w2 * g2) * iarea;
                var scan_b = (w0 * b0 + w1 * b1 + w2 * b2) * iarea;
                var scan_u = (w0 * tu0 + w1 * tu1 + w2 * tu2) * iarea;
                var scan_v = (w0 * tv0 + w1 * tv1 + w2 * tv2) * iarea;

                var x: i32 = min_x;
                @setRuntimeSafety(false);
                while (x <= max_x) : (x += 1) {
                    if (w0 >= 0 and w1 >= 0 and w2 >= 0) {
                        const zb_idx = y_base + @as(usize, @intCast(x));
                        if (scan_z < zb[zb_idx]) {
                            zb[zb_idx] = scan_z;
                            const ir = @max(0, @min(255, @as(i32, @intFromFloat(scan_r))));
                            const ig = @max(0, @min(255, @as(i32, @intFromFloat(scan_g))));
                            const ib = @max(0, @min(255, @as(i32, @intFromFloat(scan_b))));
                            const tex_color = sampleTexture(texture, scan_u, scan_v);
                            const vert_color = Color{ .b = @truncate(ib), .g = @truncate(ig), .r = @truncate(ir), .a = 255 };
                            const final_color = modulateColor(vert_color, tex_color);
                            const fb_idx = zb_idx * PIXEL_BYTES;
                            fb[fb_idx + 0] = final_color.b;
                            fb[fb_idx + 1] = final_color.g;
                            fb[fb_idx + 2] = final_color.r;
                            fb[fb_idx + 3] = 255;
                        }
                    }
                    w0 += dw0_dx;
                    w1 += dw1_dx;
                    w2 += dw2_dx;
                    scan_z += dz_dx;
                    scan_r += dr_dx;
                    scan_g += dg_dx;
                    scan_b += db_dx;
                    scan_u += du_dx;
                    scan_v += dv_dx;
                }
                @setRuntimeSafety(true);
            }
        }
    }

    /// Render a triangle into a tile-local Z/color buffer at screen position (tile_x, tile_y).
    ///
    /// Renders into caller-provided tile-local buffers without touching the
    /// global framebuffer. After ALL triangles for a tile have been rendered,
    /// the caller copies tile_cb to the framebuffer.
    ///
    /// Contract:
    /// - tile_zb and tile_cb must be TILE_SIZE × TILE_SIZE each
    /// - tile_zb initialized to 1.0 (far clip), tile_cb initialized to 0 (clear)
    /// - tile_x/tile_y are screen-space pixel coordinates of the tile's top-left corner
    pub fn drawTriangleTile(self: *SoftwareBackend, v0: Vertex, v1: Vertex, v2: Vertex, texture: []const u8, tile_x: u32, tile_y: u32, tile_zb: []f32, tile_cb: []u32) void {
        // ---------------------------------------------------------------
        // 1. Sort vertices by Y (bubble sort, 3 elements)
        // ---------------------------------------------------------------
        var v = [_]Vertex{ v0, v1, v2 };
        if (v[0].y > v[1].y) { const t = v[0]; v[0] = v[1]; v[1] = t; }
        if (v[1].y > v[2].y) { const t = v[1]; v[1] = v[2]; v[2] = t; }
        if (v[0].y > v[1].y) { const t = v[0]; v[0] = v[1]; v[1] = t; }

        // ---------------------------------------------------------------
        // 2. Compute area and early-out for back-face / degenerate
        // ---------------------------------------------------------------
        const area = (v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[1].y - v[0].y) * (v[2].x - v[0].x);
        if (@abs(area) < 0.001) return;
        const inv_area = 1.0 / area;

        // ---------------------------------------------------------------
        // 3. DDA coefficients — edge function increments per pixel step
        // ---------------------------------------------------------------
        const dw0_dx = v[1].y - v[2].y;
        const dw1_dx = v[2].y - v[0].y;
        const dw2_dx = v[0].y - v[1].y;

        // ---------------------------------------------------------------
        // 4. Intersect triangle AABB with tile bounds
        // ---------------------------------------------------------------
        const tile_x_end = tile_x + TILE_SIZE;
        const tile_y_end = tile_y + TILE_SIZE;

        const tri_min_x = @as(i32, @intFromFloat(@min(v[0].x, @min(v[1].x, v[2].x))));
        const tri_max_x = @as(i32, @intFromFloat(@max(v[0].x, @max(v[1].x, v[2].x))));
        const tri_min_y = @as(i32, @intFromFloat(v[0].y));
        const tri_max_y = @as(i32, @intFromFloat(v[2].y));

        const y_start = @max(@max(tri_min_y, @as(i32, @intCast(tile_y))), 0);
        const y_end = @min(@min(tri_max_y, @as(i32, @intCast(tile_y_end - 1))), @as(i32, @intCast(self.height - 1)));
        if (y_start > y_end) return;

        const min_x = @max(@max(tri_min_x, @as(i32, @intCast(tile_x))), 0);
        const max_x = @min(@min(tri_max_x, @as(i32, @intCast(tile_x_end - 1))), @as(i32, @intCast(self.width - 1)));
        if (min_x > max_x) return;

        // Pre-fetch vertex attributes.
        const z0 = v[0].z; const z1 = v[1].z; const z2 = v[2].z;
        const r0: f32 = @floatFromInt(v[0].color.r);
        const r1: f32 = @floatFromInt(v[1].color.r);
        const r2: f32 = @floatFromInt(v[2].color.r);
        const g0: f32 = @floatFromInt(v[0].color.g);
        const g1: f32 = @floatFromInt(v[1].color.g);
        const g2: f32 = @floatFromInt(v[2].color.g);
        const b0: f32 = @floatFromInt(v[0].color.b);
        const b1: f32 = @floatFromInt(v[1].color.b);
        const b2: f32 = @floatFromInt(v[2].color.b);
        const tu0 = v[0].u; const tu1 = v[1].u; const tu2 = v[2].u;
        const tv0 = v[0].v; const tv1 = v[1].v; const tv2 = v[2].v;

        const iarea = inv_area;
        const dz_dx = (dw0_dx * z0 + dw1_dx * z1 + dw2_dx * z2) * iarea;
        const dr_dx = (dw0_dx * r0 + dw1_dx * r1 + dw2_dx * r2) * iarea;
        const dg_dx = (dw0_dx * g0 + dw1_dx * g1 + dw2_dx * g2) * iarea;
        const db_dx = (dw0_dx * b0 + dw1_dx * b1 + dw2_dx * b2) * iarea;
        const du_dx = (dw0_dx * tu0 + dw1_dx * tu1 + dw2_dx * tu2) * iarea;
        const dv_dx = (dw0_dx * tv0 + dw1_dx * tv1 + dw2_dx * tv2) * iarea;

        // ---------------------------------------------------------------
        // 5. Scanline fill — render into tile-local Z/color buffers
        // ---------------------------------------------------------------
        const local_pitch: u32 = TILE_SIZE;
        const tile_x_i: i32 = @intCast(tile_x);
        const tile_y_i: i32 = @intCast(tile_y);

        var y: i32 = y_start;
        while (y <= y_end) : (y += 1) {
            const fy: f32 = @floatFromInt(y);

            // Compute w0,w1,w2 at start-of-scanline (x = min_x).
            const fx0: f32 = @floatFromInt(min_x);
            var w0 = (v[2].x - v[1].x) * (fy - v[1].y) - (v[2].y - v[1].y) * (fx0 - v[1].x);
            var w1 = (v[0].x - v[2].x) * (fy - v[2].y) - (v[0].y - v[2].y) * (fx0 - v[2].x);
            var w2 = (v[1].x - v[0].x) * (fy - v[0].y) - (v[1].y - v[0].y) * (fx0 - v[0].x);

            var scan_z = (w0 * z0 + w1 * z1 + w2 * z2) * iarea;
            var scan_r = (w0 * r0 + w1 * r1 + w2 * r2) * iarea;
            var scan_g = (w0 * g0 + w1 * g1 + w2 * g2) * iarea;
            var scan_b = (w0 * b0 + w1 * b1 + w2 * b2) * iarea;
            var scan_u = (w0 * tu0 + w1 * tu1 + w2 * tu2) * iarea;
            var scan_v = (w0 * tv0 + w1 * tv1 + w2 * tv2) * iarea;

            const local_y = @as(u32, @intCast(y - tile_y_i));
            const tile_row_base = local_y * local_pitch;

            var x: i32 = min_x;
            @setRuntimeSafety(false);
            while (x <= max_x) : (x += 1) {
                if (w0 >= 0 and w1 >= 0 and w2 >= 0) {
                    const local_x = @as(u32, @intCast(x - tile_x_i));
                    const tile_idx = tile_row_base + local_x;
                    if (scan_z < tile_zb[tile_idx]) {
                        tile_zb[tile_idx] = scan_z;
                        const ir = @max(0, @min(255, @as(i32, @intFromFloat(scan_r))));
                        const ig = @max(0, @min(255, @as(i32, @intFromFloat(scan_g))));
                        const ib = @max(0, @min(255, @as(i32, @intFromFloat(scan_b))));
                        const tex_color = sampleTexture(texture, scan_u, scan_v);
                        const vert_color = Color{ .b = @truncate(ib), .g = @truncate(ig), .r = @truncate(ir), .a = 255 };
                        const final_color = modulateColor(vert_color, tex_color);
                        tile_cb[tile_idx] = @bitCast([4]u8{ final_color.b, final_color.g, final_color.r, 255 });
                    }
                }
                w0 += dw0_dx;
                w1 += dw1_dx;
                w2 += dw2_dx;
                scan_z += dz_dx;
                scan_r += dr_dx;
                scan_g += dg_dx;
                scan_b += db_dx;
                scan_u += du_dx;
                scan_v += dv_dx;
            }
            @setRuntimeSafety(true);
        }
    }

    /// Copy a TILE_SIZE × TILE_SIZE color buffer to the framebuffer at (tile_x, tile_y).
    /// Clamps to framebuffer bounds for edge tiles.
    pub fn copyTileToFb(self: *SoftwareBackend, tile_x: u32, tile_y: u32, tile_cb: []const u32, fb_w: u32, fb_h: u32) void {
        const fb_u32: []u32 = @ptrCast(@alignCast(self.fb));
        var row: u32 = 0;
        while (row < TILE_SIZE) : (row += 1) {
            const dst_row = tile_y + row;
            if (dst_row >= fb_h) break;
            const dst_start = dst_row * fb_w + tile_x;
            const src_start = row * TILE_SIZE;
            const copy_cols = @min(TILE_SIZE, fb_w - tile_x);
            const src_slice = tile_cb[src_start..][0..copy_cols];
            const dst_slice = fb_u32[dst_start..][0..copy_cols];
            @memcpy(dst_slice, src_slice);
        }
    }

    fn putPixel(self: *SoftwareBackend, x: i32, y: i32, color: Color) void {
        const w: i32 = @intCast(self.width);
        const h: i32 = @intCast(self.height);
        if (x < 0 or x >= w or y < 0 or y >= h) return;
        const idx = (@as(usize, @intCast(y)) * @as(usize, w) + @as(usize, @intCast(x))) * PIXEL_BYTES;
        self.fb[idx + 0] = color.b;
        self.fb[idx + 1] = color.g;
        self.fb[idx + 2] = color.r;
        self.fb[idx + 3] = color.a;
    }

    fn sampleTexture(tex: []const u8, u: f32, v: f32) Color {
        const u_clamped = @max(0.0, @min(1.0, u));
        const v_clamped = @max(0.0, @min(1.0, v));
        const tx = @min(@as(u32, @intFromFloat(u_clamped * @as(f32, @floatFromInt(TEXTURE_W - 1)))), TEXTURE_W - 1);
        const ty = @min(@as(u32, @intFromFloat(v_clamped * @as(f32, @floatFromInt(TEXTURE_H - 1)))), TEXTURE_H - 1);
        const idx = (@as(usize, ty) * TEXTURE_W + @as(usize, tx)) * 4;
        return Color{
            .b = tex[idx + 0],
            .g = tex[idx + 1],
            .r = tex[idx + 2],
            .a = tex[idx + 3],
        };
    }

    // ═══════════════════════════════════════════════════════════════
    // Fields (MUST come after declarations in Zig 0.16)
    // ═══════════════════════════════════════════════════════════════

    allocator: std.mem.Allocator,
    /// X11 Display pointer — NOT the full Window struct (avoids a dangling pointer
    /// when Engine is returned from init() and the backend self-pointer becomes stale).
    /// Set once during init(), never modified.
    display: *x11.Display,
    /// X11 Window handle for XPutImage.
    handle: x11.Window,
    fb: []u8,
    zb: []f32,
    width: u32,
    height: u32,
    gc: x11.GC,
    ximage_data: []u8,

    /// Thread pool borrowed from Engine (strip rendering).
    pool: ?*ThreadPool = null,
    thread_count: u32 = 0,

    /// SIMD acceleration enabled (AVX2 @Vector(8, f32) inner loop).
    use_simd: bool = false,
};

/// Generate a sphere mesh as un-indexed triangles using a lat/long grid.
/// `rings` and `segments` must be >= 3 or an empty slice is returned.
/// Returns 2 * rings * segments triangles (each triangle has 3 vertices).
pub fn generateSphere(allocator: std.mem.Allocator, rings: u32, segments: u32, radius: f32) ![]Vertex {
    if (rings < 3 or segments < 3) return &[_]Vertex{};

    const tri_count = rings * segments * 2;
    const vert_count = tri_count * 3;
    const verts = try allocator.alloc(Vertex, vert_count);

    var idx: usize = 0;
    var r: u32 = 0;
    while (r < rings) : (r += 1) {
        const theta1 = @as(f32, @floatFromInt(r)) / @as(f32, @floatFromInt(rings)) * std.math.pi;
        const theta2 = @as(f32, @floatFromInt(r + 1)) / @as(f32, @floatFromInt(rings)) * std.math.pi;
        const sin_t1 = std.math.sin(theta1);
        const cos_t1 = std.math.cos(theta1);
        const sin_t2 = std.math.sin(theta2);
        const cos_t2 = std.math.cos(theta2);

        var s: u32 = 0;
        while (s < segments) : (s += 1) {
            const phi1 = @as(f32, @floatFromInt(s)) / @as(f32, @floatFromInt(segments)) * 2.0 * std.math.pi;
            const phi2 = @as(f32, @floatFromInt(s + 1)) / @as(f32, @floatFromInt(segments)) * 2.0 * std.math.pi;
            const sin_p1 = std.math.sin(phi1);
            const cos_p1 = std.math.cos(phi1);
            const sin_p2 = std.math.sin(phi2);
            const cos_p2 = std.math.cos(phi2);

            // Direction vectors for the 4 quad corners (unit length)
            const d0x = sin_t1 * cos_p1;
            const d0y = cos_t1;
            const d0z = sin_t1 * sin_p1;
            const d1x = sin_t1 * cos_p2;
            const d1y = cos_t1;
            const d1z = sin_t1 * sin_p2;
            const d2x = sin_t2 * cos_p1;
            const d2y = cos_t2;
            const d2z = sin_t2 * sin_p1;
            const d3x = sin_t2 * cos_p2;
            const d3y = cos_t2;
            const d3z = sin_t2 * sin_p2;

            const uv_u1 = phi1 / (2.0 * std.math.pi);
            const uv_u2 = phi2 / (2.0 * std.math.pi);
            const uv_v1 = theta1 / std.math.pi;
            const uv_v2 = theta2 / std.math.pi;

            // Triangle 1: (0, 1, 2) = top-left, top-right, bottom-left
            verts[idx] = Vertex{
                .x = d0x * radius, .y = d0y * radius, .z = d0z * radius,
                .nx = d0x, .ny = d0y, .nz = d0z,
                .u = uv_u1, .v = uv_v1, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
            verts[idx] = Vertex{
                .x = d1x * radius, .y = d1y * radius, .z = d1z * radius,
                .nx = d1x, .ny = d1y, .nz = d1z,
                .u = uv_u2, .v = uv_v1, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
            verts[idx] = Vertex{
                .x = d2x * radius, .y = d2y * radius, .z = d2z * radius,
                .nx = d2x, .ny = d2y, .nz = d2z,
                .u = uv_u1, .v = uv_v2, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;

            // Triangle 2: (1, 3, 2) = top-right, bottom-right, bottom-left
            verts[idx] = Vertex{
                .x = d1x * radius, .y = d1y * radius, .z = d1z * radius,
                .nx = d1x, .ny = d1y, .nz = d1z,
                .u = uv_u2, .v = uv_v1, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
            verts[idx] = Vertex{
                .x = d3x * radius, .y = d3y * radius, .z = d3z * radius,
                .nx = d3x, .ny = d3y, .nz = d3z,
                .u = uv_u2, .v = uv_v2, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
            verts[idx] = Vertex{
                .x = d2x * radius, .y = d2y * radius, .z = d2z * radius,
                .nx = d2x, .ny = d2y, .nz = d2z,
                .u = uv_u1, .v = uv_v2, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
        }
    }

    return verts;
}

/// Generate a torus mesh as un-indexed triangles.
/// `segments` divisions around both the major and minor circles.
/// `major_r` must be > `minor_r`, and `segments` must be >= 3.
pub fn generateTorus(allocator: std.mem.Allocator, major_r: f32, minor_r: f32, segments: u32) ![]Vertex {
    if (major_r <= minor_r) return &[_]Vertex{};
    if (segments < 3) return &[_]Vertex{};

    const tri_count = segments * segments * 2;
    const vert_count = tri_count * 3;
    const verts = try allocator.alloc(Vertex, vert_count);

    var idx: usize = 0;
    var i: u32 = 0;
    while (i < segments) : (i += 1) {
        const theta = @as(f32, @floatFromInt(i)) / @as(f32, @floatFromInt(segments)) * 2.0 * std.math.pi;
        const theta_n = @as(f32, @floatFromInt(i + 1)) / @as(f32, @floatFromInt(segments)) * 2.0 * std.math.pi;
        const cos_t = std.math.cos(theta);
        const sin_t = std.math.sin(theta);
        const cos_tn = std.math.cos(theta_n);
        const sin_tn = std.math.sin(theta_n);

        var j: u32 = 0;
        while (j < segments) : (j += 1) {
            const phi = @as(f32, @floatFromInt(j)) / @as(f32, @floatFromInt(segments)) * 2.0 * std.math.pi;
            const phi_n = @as(f32, @floatFromInt(j + 1)) / @as(f32, @floatFromInt(segments)) * 2.0 * std.math.pi;
            const cos_p = std.math.cos(phi);
            const sin_p = std.math.sin(phi);
            const cos_pn = std.math.cos(phi_n);
            const sin_pn = std.math.sin(phi_n);

            // Tube centers on the major ring
            const cx = major_r * cos_t;
            const cz = major_r * sin_t;
            const cxn = major_r * cos_tn;
            const czn = major_r * sin_tn;

            // Normal directions for the 4 tube vertices
            const nx0 = cos_p * cos_t;
            const ny0 = sin_p;
            const nz0 = cos_p * sin_t;
            const nx1 = cos_pn * cos_t;
            const ny1 = sin_pn;
            const nz1 = cos_pn * sin_t;
            const nx2 = cos_p * cos_tn;
            const ny2 = sin_p;
            const nz2 = cos_p * sin_tn;
            const nx3 = cos_pn * cos_tn;
            const ny3 = sin_pn;
            const nz3 = cos_pn * sin_tn;

            // Positions: center + minor_r * normal
            const p0x = cx + minor_r * nx0;
            const p0y = minor_r * ny0;
            const p0z = cz + minor_r * nz0;
            const p1x = cx + minor_r * nx1;
            const p1y = minor_r * ny1;
            const p1z = cz + minor_r * nz1;
            const p2x = cxn + minor_r * nx2;
            const p2y = minor_r * ny2;
            const p2z = czn + minor_r * nz2;
            const p3x = cxn + minor_r * nx3;
            const p3y = minor_r * ny3;
            const p3z = czn + minor_r * nz3;

            const uv_u0 = @as(f32, @floatFromInt(i)) / @as(f32, @floatFromInt(segments));
            const uv_v0 = @as(f32, @floatFromInt(j)) / @as(f32, @floatFromInt(segments));
            const uv_u1 = @as(f32, @floatFromInt(i + 1)) / @as(f32, @floatFromInt(segments));
            const uv_v1 = @as(f32, @floatFromInt(j + 1)) / @as(f32, @floatFromInt(segments));

            // Triangle 1: (0, 1, 2) = (theta,phi), (theta,phi_next), (theta_next,phi)
            verts[idx] = Vertex{
                .x = p0x, .y = p0y, .z = p0z,
                .nx = nx0, .ny = ny0, .nz = nz0,
                .u = uv_u0, .v = uv_v0, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
            verts[idx] = Vertex{
                .x = p1x, .y = p1y, .z = p1z,
                .nx = nx1, .ny = ny1, .nz = nz1,
                .u = uv_u0, .v = uv_v1, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
            verts[idx] = Vertex{
                .x = p2x, .y = p2y, .z = p2z,
                .nx = nx2, .ny = ny2, .nz = nz2,
                .u = uv_u1, .v = uv_v0, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;

            // Triangle 2: (1, 3, 2) = (theta,phi_next), (theta_next,phi_next), (theta_next,phi)
            verts[idx] = Vertex{
                .x = p1x, .y = p1y, .z = p1z,
                .nx = nx1, .ny = ny1, .nz = nz1,
                .u = uv_u0, .v = uv_v1, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
            verts[idx] = Vertex{
                .x = p3x, .y = p3y, .z = p3z,
                .nx = nx3, .ny = ny3, .nz = nz3,
                .u = uv_u1, .v = uv_v1, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
            verts[idx] = Vertex{
                .x = p2x, .y = p2y, .z = p2z,
                .nx = nx2, .ny = ny2, .nz = nz2,
                .u = uv_u1, .v = uv_v0, .inv_w = 1.0,
                .color = .{ .r = 255, .g = 255, .b = 255, .a = 255 },
            };
            idx += 1;
        }
    }

    return verts;
}

/// Generate a 64×64 procedural brick texture in BGRA32 format.
/// Each brick is 16×8 pixels with a 2px mortar gap (40,40,40).
/// Odd rows are staggered by 8px for the running-bond pattern.
/// Brick colors are derived from a deterministic hash of (row, col).
pub fn generateBrickTexture() [TEXTURE_W * TEXTURE_H * 4]u8 {
    var tex: [TEXTURE_W * TEXTURE_H * 4]u8 = undefined;

    for (0..TEXTURE_H) |py| {
        for (0..TEXTURE_W) |px| {
            const brick_row = py / 8;
            const is_odd = brick_row % 2 == 1;
            const offset_x: usize = if (is_odd) 8 else 0;

            const cell_x = (px + offset_x) % 16;
            const cell_y = py % 8;

            const is_mortar = cell_x < 2 or cell_x >= 14 or cell_y < 2 or cell_y >= 6;

            const idx = (py * TEXTURE_W + px) * 4;
            if (is_mortar) {
                tex[idx + 0] = 40; // b
                tex[idx + 1] = 40; // g
                tex[idx + 2] = 40; // r
                tex[idx + 3] = 255; // a
            } else {
                const brick_col = ((px + offset_x) % TEXTURE_W) / 16;
                const h = brickHash(brick_row, brick_col);
                tex[idx + 0] = @truncate(40 + (h % 41));  // b: 40-80
                tex[idx + 1] = @truncate(60 + (h % 41));  // g: 60-100
                tex[idx + 2] = @truncate(160 + (h % 41)); // r: 160-200
                tex[idx + 3] = 255;
            }
        }
    }

    return tex;
}

/// Simple deterministic hash for brick row/column -> color seed.
fn brickHash(row: usize, col: usize) u32 {
    var h: u32 = @truncate((row + 1) *% 374761393 +% (col + 1) *% 668265263);
    h ^= h >> 13;
    h *%= 1274126177;
    h ^= h >> 16;
    return h;
}

pub fn generateCheckerboard() [TEXTURE_W * TEXTURE_H * 4]u8 {
    var tex: [TEXTURE_W * TEXTURE_H * 4]u8 = undefined;
    const check_size = 8;
    for (0..TEXTURE_H) |y| {
        for (0..TEXTURE_W) |x| {
            const cx = x / check_size;
            const cy = y / check_size;
            const is_white = (cx + cy) % 2 == 0;
            const idx = (@as(usize, y) * TEXTURE_W + @as(usize, x)) * 4;
            if (is_white) {
                tex[idx + 0] = 220;
                tex[idx + 1] = 200;
                tex[idx + 2] = 255;
                tex[idx + 3] = 255;
            } else {
                tex[idx + 0] = 80;
                tex[idx + 1] = 50;
                tex[idx + 2] = 40;
                tex[idx + 3] = 255;
            }
        }
    }
    return tex;
}
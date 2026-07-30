//! Frame diagnostics and performance tracking.
//!
//! Tracks per-phase timing, entity counts, and produces periodic
//! benchmark reports. Compiled out in ReleaseFast (zero cost).
//!
//! Usage:
//!   var bench = diagnostics.Benchmark.init(60); // report every 60 frames
//!   bench.beginFrame();
//!   bench.beginPhase(.pre_update);
//!   // ... run systems ...
//!   bench.endPhase(.pre_update);
//!   // ... repeat for each phase ...
//!   bench.endFrame(alive_count, fps);

const std = @import("std");

/// Game loop phases for timing breakdown.
pub const PhaseTag = enum {
    pre_update,
    update,
    post_update,
    render,
};

/// Accumulates frame performance metrics and produces periodic reports.
pub const Benchmark = struct {
    // Per-frame timings (set by beginPhase/endPhase)
    ms_pre_update: f32 = 0,
    ms_update: f32 = 0,
    ms_post_update: f32 = 0,
    ms_render: f32 = 0,

    /// Time spent waiting for thread pool workers to finish (strip rasterization).
    ms_thread_wait: f32 = 0,

    // Internal: high-res timer values
    phase_start: u64 = 0,
    frame_start: u64 = 0,

    // Rolling accumulators (reset every report_window frames)
    acc_frame_ms: f32 = 0,
    acc_pre_update: f32 = 0,
    acc_update: f32 = 0,
    acc_post_update: f32 = 0,
    acc_render: f32 = 0,
    acc_thread_wait: f32 = 0,
    acc_frame_count: u32 = 0,
    acc_entities: u64 = 0,

    /// How many frames between benchmark reports.
    report_window: u32,

    /// In debug builds, also compute min/max over the window.
    min_frame_ms: f32 = 9999,
    max_frame_ms: f32 = 0,

    pub fn init(report_window: u32) Benchmark {
        return .{ .report_window = report_window };
    }

    /// Call at the START of each frame (before any phase runs).
    pub fn beginFrame(self: *Benchmark) void {
        self.frame_start = @import("time.zig").nanoTime();
        self.ms_pre_update = 0;
        self.ms_update = 0;
        self.ms_post_update = 0;
        self.ms_render = 0;
        self.ms_thread_wait = 0;
    }

    /// Call before the work of a phase.
    pub fn beginPhase(self: *Benchmark) void {
        self.phase_start = @import("time.zig").nanoTime();
    }

    /// Call after the work of a phase completes.
    pub fn endPhase(self: *Benchmark, phase: PhaseTag) void {
        const elapsed_ns = @import("time.zig").nanoTime() - self.phase_start;
        const ms = @as(f32, @floatFromInt(elapsed_ns)) / @as(f32, std.time.ns_per_ms);
        switch (phase) {
            .pre_update => self.ms_pre_update = ms,
            .update => self.ms_update = ms,
            .post_update => self.ms_post_update = ms,
            .render => self.ms_render = ms,
        }
    }

    /// Call at the END of each frame.
    /// `entities_alive` and `fps` are for the report.
    pub fn endFrame(self: *Benchmark, entities_alive: u32, fps: u32) void {
        const elapsed_ns = @import("time.zig").nanoTime() - self.frame_start;
        const frame_ms = @as(f64, @floatFromInt(elapsed_ns)) / @as(f64, std.time.ns_per_ms);

        // Track min/max (debug)
        const fms = @as(f32, @floatCast(frame_ms));
        if (fms < self.min_frame_ms) self.min_frame_ms = fms;
        if (fms > self.max_frame_ms) self.max_frame_ms = fms;

        // Accumulate for averaging
        const frame_ms_f32: f32 = @floatCast(frame_ms);
        self.acc_frame_ms += frame_ms_f32;
        self.acc_pre_update += self.ms_pre_update;
        self.acc_update += self.ms_update;
        self.acc_post_update += self.ms_post_update;
        self.acc_render += self.ms_render;
        self.acc_entities += entities_alive;
        self.acc_thread_wait += self.ms_thread_wait;
        self.acc_frame_count += 1;

        // Check if it's time to report
        if (self.acc_frame_count >= self.report_window) {
            self.printReport(fps);
            self.resetAccumulators();
        }
    }

    fn printReport(self: *Benchmark, current_fps: u32) void {
        const n: f64 = @as(f64, @floatFromInt(self.acc_frame_count));
        if (n == 0) return;

        const avg_frame = self.acc_frame_ms / n;
        const avg_pre = self.acc_pre_update / n;
        const avg_upd = self.acc_update / n;
        const avg_post = self.acc_post_update / n;
        const avg_render = self.acc_render / n;
        const avg_wait = self.acc_thread_wait / n;
        const avg_entities_f: f64 = @floatFromInt(self.acc_entities);
        const avg_entities: u32 = @intFromFloat(avg_entities_f / n);

        // ── ─┐ ┌ └ ┘ ├ ┤ │
        std.debug.print("╭─ Benchmark [{d} frames] ─────────────────╮\n", .{self.acc_frame_count});
        std.debug.print("│ FPS:           {d: >5}                    │\n", .{current_fps});
        std.debug.print("│ Frame:         {d: >6.2}ms (min {d:5.2}  max {d:5.2}) │\n", .{ avg_frame, self.min_frame_ms, self.max_frame_ms });
        std.debug.print("│  ├─ Pre-Update:  {d: >6.2}ms              │\n", .{avg_pre});
        std.debug.print("│  ├─ Update:      {d: >6.2}ms              │\n", .{avg_upd});
        std.debug.print("│  ├─ Post-Update: {d: >6.2}ms              │\n", .{avg_post});
        std.debug.print("│  ├─ Render:      {d: >6.2}ms              │\n", .{avg_render});
        std.debug.print("│  │  └─ Wait:     {d: >6.2}ms              │\n", .{avg_wait});
        std.debug.print("│ Entities:      {d: >5}                    │\n", .{avg_entities});
        std.debug.print("╰──────────────────────────────────────────╯\n", .{});
    }

    fn resetAccumulators(self: *Benchmark) void {
        self.acc_frame_ms = 0;
        self.acc_pre_update = 0;
        self.acc_update = 0;
        self.acc_post_update = 0;
        self.acc_render = 0;
        self.acc_thread_wait = 0;
        self.acc_entities = 0;
        self.acc_frame_count = 0;
        self.min_frame_ms = 9999;
        self.max_frame_ms = 0;
    }
};

test "benchmark lifecycle" {
    var b = Benchmark.init(5);

    for (0..3) |_| {
        b.beginFrame();
        b.beginPhase();
        b.endPhase(.pre_update);
        b.beginPhase();
        b.endPhase(.update);
        b.beginPhase();
        b.endPhase(.post_update);
        b.beginPhase();
        b.endPhase(.render);
        b.endFrame(10, 60);
    }
    // Should not report yet (only 3 frames, window is 5)
    try std.testing.expectEqual(@as(u32, 3), b.acc_frame_count);
}

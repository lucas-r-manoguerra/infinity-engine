//! Infinity Engine — Performance Benchmark Runner
//!
//! Always built in ReleaseSafe for meaningful numbers.
//! Usage: `zig build bench`

const std = @import("std");
const time = @import("core/time.zig");

const benchs_math = @import("benchs_math.zig");
const benchs_ecs = @import("benchs_ecs.zig");
const benchs_memory = @import("benchs_memory.zig");
const benchs_renderer = @import("benchs_renderer.zig");

pub fn main() void {
    std.debug.print("\n", .{});
    std.debug.print("╔══════════════════════════════════════════════╗\n", .{});
    std.debug.print("║   Infinity Engine — Performance Benchmarks  ║\n", .{});
    std.debug.print("╚══════════════════════════════════════════════╝\n", .{});

    const start = time.nanoTime();

    benchs_math.run();
    benchs_ecs.run();
    benchs_memory.run();
    benchs_renderer.run();

    const elapsed = time.nanoTime() - start;
    const elapsed_ms: f64 = @as(f64, @floatFromInt(elapsed)) / 1_000_000.0;
    std.debug.print("\n  total: {d:.1} ms\n\n", .{elapsed_ms});
}

//! Benchmarks: core/memory (ArenaAllocator, PoolAllocator, FrameAllocator)
//! Priority P1.
//!
//! Run via: `zig build bench`

const std = @import("std");
const time = @import("core/time.zig");

const ITERATIONS = 100_000;
const ALLOC_SIZE = 64;

const allocator = std.heap.page_allocator;

// ---------------------------------------------------------------------------
// ArenaAllocator
// ---------------------------------------------------------------------------

fn benchArenaAlloc() void {
    var arena = std.heap.ArenaAllocator.init(allocator);
    defer arena.deinit();

    const arena_alloc = arena.allocator();
    var total: usize = 0;
    for (0..ITERATIONS) |_| {
        const buf = arena_alloc.alloc(u8, ALLOC_SIZE) catch unreachable;
        total += buf.len;
    }
    _ = &total;
}

fn benchArenaAllocReset() void {
    var arena = std.heap.ArenaAllocator.init(allocator);
    defer arena.deinit();

    const arena_alloc = arena.allocator();
    for (0..10) |_| {
        for (0..ITERATIONS / 10) |_| {
            const buf = arena_alloc.alloc(u8, ALLOC_SIZE) catch unreachable;
            _ = buf;
        }
        _ = arena.reset(.retain_capacity);
    }
}

// ---------------------------------------------------------------------------
// FixedBufferAllocator
// ---------------------------------------------------------------------------

fn benchFixedBufferAlloc() void {
    var buf: [ALLOC_SIZE * ITERATIONS]u8 = undefined;
    var fba = std.heap.FixedBufferAllocator.init(&buf);
    const fba_alloc = fba.allocator();

    var total: usize = 0;
    for (0..ITERATIONS) |_| {
        const slice = fba_alloc.alloc(u8, ALLOC_SIZE) catch unreachable;
        total += slice.len;
    }
    _ = &total;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

pub fn run() void {
    std.debug.print("\n  ~ memory benchmarks ~\n", .{});

    bench("arena.alloc", benchArenaAlloc);
    bench("arena.alloc+reset", benchArenaAllocReset);
    bench("fixed_buffer.alloc", benchFixedBufferAlloc);
}

fn bench(comptime name: []const u8, func: *const fn () void) void {
    const start = time.nanoTime();
    func();
    const elapsed = time.nanoTime() - start;
    const elapsed_f: f64 = @as(f64, @floatFromInt(elapsed));
    const iters_f: f64 = @as(f64, @floatFromInt(ITERATIONS));
    const ns_per_op = elapsed_f / iters_f;
    std.debug.print("  {s: <30}  {d: >8.1} ns/op\n", .{ name, ns_per_op });
}

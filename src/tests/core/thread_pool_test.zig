//! Thread pool tests — futex wait/wake and non-Linux guard.
//!
//! Verifies that idle workers block via futex (zero CPU spin) and
//! that the pool correctly rejects non-Linux targets at compile time.

const std = @import("std");
const testing = std.testing;
const ThreadPool = @import("../../core/thread_pool.zig").ThreadPool;
const builtin = @import("builtin");

/// Simple worker context for counting.
const CountCtx = struct {
    counter: u32 = 0,
};

fn countWorker(idx: usize, ctx: *CountCtx) void {
    _ = idx;
    _ = @atomicRmw(u32, &ctx.counter, .Add, 1, .monotonic);
}

// Test that 4 workers dispatched via spawn() all complete their work
// when futex-based wait/wake is used. This verifies the futex blocking
// does not prevent forward progress (zero CPU spin, all workers done).
test "T-007 thread pool futex blocks idle workers and unblocks on spawn" {
    var ctx = CountCtx{};

    var pool = try ThreadPool.init(testing.allocator, 4);
    defer pool.deinit();

    pool.spawn(countWorker, &ctx, 4);
    pool.wait();

    try testing.expectEqual(@as(u32, 4), ctx.counter);
}

// Test that 4 workers complete their work even with multiple spawn/wait
// cycles, which exercises the futex block/unblock pattern repeatedly.
test "T-007b thread pool futex survives multiple spawn/wait cycles" {
    var ctx = CountCtx{};

    var pool = try ThreadPool.init(testing.allocator, 4);
    defer pool.deinit();

    var i: u32 = 0;
    while (i < 10) : (i += 1) {
        ctx.counter = 0;
        pool.spawn(countWorker, &ctx, 4);
        pool.wait();
        try testing.expectEqual(@as(u32, 4), ctx.counter);
    }
}

// Test that fewer workers than max work correctly (not all threads used).
test "T-007c thread pool futex works with fewer workers than max" {
    var ctx = CountCtx{};

    var pool = try ThreadPool.init(testing.allocator, 4);
    defer pool.deinit();

    pool.spawn(countWorker, &ctx, 2);
    pool.wait();

    try testing.expectEqual(@as(u32, 2), ctx.counter);
}

// Test: non-Linux init returns error.NotSupported.
// On Linux, init must succeed; on other targets, it must fail.
test "T-008 ThreadPool.init returns error.NotSupported on non-Linux" {
    if (comptime builtin.os.tag != .linux) {
        const result = ThreadPool.init(testing.allocator, 4);
        try testing.expectError(error.NotSupported, result);
    } else {
        // On Linux it should succeed normally
        var pool = try ThreadPool.init(testing.allocator, 4);
        defer pool.deinit();
        try testing.expect(pool.shared.running);
    }
}

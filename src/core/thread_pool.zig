//! Minimal thread pool for parallel rasterization.
//!
//! Zig 0.16 has no built-in `std.Thread.Pool`, so we provide a minimal
//! replacement.  Design: pre-spawn N worker threads that spin-wait on
//! per-thread atomic go/done flags.  The main thread dispatches a batch
//! of work, waits for completion, then reuses the same threads next frame.
//!
//! Usage:
//!   var pool = try ThreadPool.init(allocator, 4);
//!   defer pool.deinit();
//!   pool.spend(Worker.run, &ctx, 4);
//!   pool.wait();
//!
//! The worker function receives (index: usize, ctx: *anyopaque).

const std = @import("std");

pub const ThreadPool = struct {
    /// Heap-allocated shared state so threads can safely reference it
    /// even when the owning `ThreadPool` value is moved.
    shared: *Shared,

    pub const Shared = struct {
        allocator: std.mem.Allocator,
        threads: []std.Thread,
        thread_ctx: []ThreadCtx,

        /// Set by `spawn` before releasing workers.
        work_fn: ?*const fn (usize, *anyopaque) void = null,
        work_arg: ?*anyopaque = null,
        work_count: usize = 0,

        /// Per-thread go (1 = start) and done (1 = finished).
        go: []std.atomic.Value(u32),
        done: []std.atomic.Value(u32),

        running: bool = true,
    };

    const ThreadCtx = struct {
        shared: *Shared,
        index: usize,
    };

    /// Create the pool and spawn `thread_count` worker threads.
    pub fn init(allocator: std.mem.Allocator, thread_count: usize) !ThreadPool {
        const shared = try allocator.create(Shared);
        errdefer allocator.destroy(shared);

        const threads = try allocator.alloc(std.Thread, thread_count);
        errdefer allocator.free(threads);

        const tctx = try allocator.alloc(ThreadCtx, thread_count);
        errdefer allocator.free(tctx);

        const go = try allocator.alloc(std.atomic.Value(u32), thread_count);
        errdefer allocator.free(go);

        const done = try allocator.alloc(std.atomic.Value(u32), thread_count);
        errdefer allocator.free(done);

        for (0..thread_count) |i| {
            go[i] = std.atomic.Value(u32).init(0);
            done[i] = std.atomic.Value(u32).init(0);
        }

        shared.* = Shared{
            .allocator = allocator,
            .threads = threads,
            .thread_ctx = tctx,
            .go = go,
            .done = done,
        };

        for (0..thread_count) |i| {
            tctx[i] = .{ .shared = shared, .index = i };
            threads[i] = try std.Thread.spawn(.{}, workerFn, .{&tctx[i]});
        }

        return ThreadPool{ .shared = shared };
    }

    /// Signal shutdown, join all threads, free memory.
    pub fn deinit(self: *ThreadPool) void {
        self.shared.running = false;
        for (self.shared.threads) |*t| {
            t.join();
        }
        const allocator = self.shared.allocator;
        allocator.free(self.shared.threads);
        allocator.free(self.shared.thread_ctx);
        allocator.free(self.shared.go);
        allocator.free(self.shared.done);
        allocator.destroy(self.shared);
    }

    /// Dispatch `count` work items.  Each worker thread with index < count
    /// will call `func(index, args)`.  The caller MUST call `wait()` before
    /// the next `spawn()`.
    ///
    /// `func` must accept `(usize, @TypeOf(args))`.
    pub fn spawn(self: *ThreadPool, comptime func: anytype, args: anytype, count: usize) void {
        const ChildType = switch (@typeInfo(@TypeOf(args))) {
            .pointer => |p| p.child,
            else => @compileError("spawn() requires a pointer type for args"),
        };
        const W = struct {
            fn wrapper(idx: usize, ctx: *anyopaque) void {
                const typed: *ChildType = @ptrCast(@alignCast(ctx));
                @call(.auto, func, .{ idx, typed });
            }
        };
        self.shared.work_fn = W.wrapper;
        self.shared.work_arg = @ptrCast(@constCast(args));
        self.shared.work_count = count;

        // Release: workers see work_fn/work_count after seeing go==1.
        for (self.shared.go[0..count]) |*g| {
            g.store(1, .release);
        }
    }

    /// Block until all dispatched work items complete.
    pub fn wait(self: *ThreadPool) void {
        for (self.shared.done) |*d| {
            while (d.load(.acquire) == 0) {
                std.Thread.yield() catch {};
            }
            // Reset for next dispatch.
            d.store(0, .release);
        }
    }

    // ---- worker loop ----

    fn workerFn(ctx: *ThreadCtx) void {
        const shared = ctx.shared;
        const idx = ctx.index;
        while (true) {
            // Busy-wait for work or shutdown.
            while (shared.go[idx].load(.acquire) == 0 and shared.running) {
                std.Thread.yield() catch {};
            }
            if (!shared.running) return;

            // Consume the go signal.
            shared.go[idx].store(0, .release);

            // Execute work.
            if (shared.work_fn) |fn_ptr| {
                if (idx < shared.work_count) {
                    fn_ptr(idx, shared.work_arg.?);
                }
            }

            // Signal completion.
            shared.done[idx].store(1, .release);
        }
    }
};

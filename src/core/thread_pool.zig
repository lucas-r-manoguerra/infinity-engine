//! Minimal thread pool for parallel rasterization.
//!
//! Zig 0.16 has no built-in `std.Thread.Pool`, so we provide a minimal
//! replacement.  Design: pre-spawn N worker threads that futex-wait on
//! their own per-thread `go` flag when idle.  The main thread sets a
//! thread's `go` flag to 1 and wakes it via `futexWake`; the worker
//! re-checks the flag (the kernel's `FUTEX_WAIT` atomically verifies
//! the value), proceeds to work, then signals completion on a per-thread
//! `done` flag that the main thread futex-waits on.
//!
//! Usage:
//!   var pool = try ThreadPool.init(allocator, 4);
//!   defer pool.deinit();
//!   pool.spawn(Worker.run, &ctx, 4);
//!   pool.wait();
//!
//! The worker function receives (index: usize, ctx: *anyopaque).
//!
//! Non-Linux targets: init() returns error.NotSupported.
//! Linux x86_64 only — futex syscall (SYS_futex = 202,
//! FUTEX_WAIT = 0, FUTEX_WAKE = 1).

const std = @import("std");
const builtin = @import("builtin");

/// Maximum number of worker threads the pool supports.
pub const MAX_WORKERS: usize = 4;

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
        /// Workers futex-wait directly on go[idx].raw.
        go: []std.atomic.Value(u32),
        done: []std.atomic.Value(u32),

        running: bool = true,
    };

    const ThreadCtx = struct {
        shared: *Shared,
        index: usize,
    };

    /// Create the pool and spawn `thread_count` worker threads.
    /// On non-Linux targets, returns error.NotSupported.
    pub fn init(allocator: std.mem.Allocator, thread_count: usize) !ThreadPool {
        if (comptime builtin.os.tag != .linux) {
            return error.NotSupported;
        }
        if (thread_count == 0 or thread_count > MAX_WORKERS) {
            return error.NotSupported;
        }

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
        // Wake every worker so they see running=false.
        for (self.shared.go) |*g| {
            _ = futexWake(&g.raw, 1);
        }
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
            // Wake this specific worker.
            _ = futexWake(&g.raw, 1);
        }
    }

    /// Block until all dispatched work items complete.
    pub fn wait(self: *ThreadPool) void {
        const count = self.shared.work_count;
        for (self.shared.done[0..count]) |*d| {
            while (d.load(.acquire) == 0) {
                // Futex-wait on the done flag.  If it changed between the
                // load and the syscall, futex returns EAGAIN immediately.
                futexWait(&d.raw, 0);
            }
            // Reset for next dispatch.
            d.store(0, .release);
        }
        self.shared.work_count = 0;
    }

    // ---- worker loop ----

    fn workerFn(ctx: *ThreadCtx) void {
        const shared = ctx.shared;
        const idx = ctx.index;
        while (true) {
            // Futex-wait directly on our per-thread go flag.
            while (shared.go[idx].load(.acquire) == 0 and shared.running) {
                futexWait(&shared.go[idx].raw, 0);
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
            // FUTEX_WAKE on done word so wait() unblocks.
            _ = futexWake(&shared.done[idx].raw, 1);
        }
    }
};

// ── Inline debug tests ──────────────────────────────────────────────

test "ThreadPool inline: init 2, spawn, wait, deinit" {
    const testing = @import("std").testing;
    const std2 = @import("std");
    std2.debug.print("\n  [pool-test] starting...\n", .{});
    var pool = try ThreadPool.init(testing.allocator, 2);
    std2.debug.print("  [pool-test] init done\n", .{});
    defer {
        std2.debug.print("  [pool-test] deinit...\n", .{});
        pool.deinit();
        std2.debug.print("  [pool-test] deinit done\n", .{});
    }

    var counter: u32 = 0;
    const Ctx = struct {
        cnt: *u32,
    };
    var ctx = Ctx{ .cnt = &counter };
    std2.debug.print("  [pool-test] spawning...\n", .{});
    pool.spawn(struct {
        fn work(idx: usize, c: *Ctx) void {
            _ = @atomicRmw(u32, c.cnt, .Add, 1, .monotonic);
            std2.debug.print("    worker {d} done\n", .{idx});
        }
    }.work, &ctx, 2);
    std2.debug.print("  [pool-test] waiting...\n", .{});
    pool.wait();
    std2.debug.print("  [pool-test] wait done, counter={d}\n", .{counter});
    try testing.expectEqual(@as(u32, 2), counter);
}

// ── Linux x86_64 futex syscall wrappers ──────────────────────────────

/// SYS_futex on x86_64.
const SYS_futex: usize = 202;
const FUTEX_WAIT: u32 = 0;
const FUTEX_WAKE: u32 = 1;

/// Block until the value at `ptr` differs from `expected`,
/// or return immediately with EAGAIN if already different.
fn futexWait(ptr: *u32, expected: u32) void {
    if (comptime builtin.os.tag != .linux) return;
    _ = asm volatile ("syscall"
        : [ret] "={rax}" (-> usize),
        : [rax] "{rax}" (SYS_futex),
          [rdi] "{rdi}" (@as(usize, @intFromPtr(ptr))),
          [rsi] "{rsi}" (@as(usize, FUTEX_WAIT)),
          [rdx] "{rdx}" (@as(usize, expected)),
          [r10] "{r10}" (@as(usize, 0)),
          [r8]  "{r8}"  (@as(usize, 0)),
          [r9]  "{r9}"  (@as(usize, 0)),
        : .{ .rcx = true, .r11 = true, .memory = true });
}

/// Wake up to `count` threads waiting on the futex at `ptr`.
/// Returns the number of threads woken.
fn futexWake(ptr: *u32, count: u32) u32 {
    if (comptime builtin.os.tag != .linux) return 0;
    return asm volatile ("syscall"
        : [ret] "={rax}" (-> u32),
        : [rax] "{rax}" (SYS_futex),
          [rdi] "{rdi}" (@as(usize, @intFromPtr(ptr))),
          [rsi] "{rsi}" (@as(usize, FUTEX_WAKE)),
          [rdx] "{rdx}" (@as(usize, count)),
          [r10] "{r10}" (@as(usize, 0)),
          [r8]  "{r8}"  (@as(usize, 0)),
          [r9]  "{r9}"  (@as(usize, 0)),
        : .{ .rcx = true, .r11 = true, .memory = true });
}

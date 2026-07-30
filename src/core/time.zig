//! Time management: clock, delta time, timers, FPS counter.
//!
//! Provides the timing foundation for the game loop.
//! Uses platform-specific high-resolution timers.

const std = @import("std");

/// Returns the current monotonic timestamp in nanoseconds (Linux).
pub fn nanoTime() u64 {
    var ts: std.os.linux.timespec = undefined;
    _ = std.os.linux.clock_gettime(.MONOTONIC, &ts);
    const sec = @as(u64, @intCast(ts.sec));
    const nsec = @as(u64, @intCast(ts.nsec));
    return sec * std.time.ns_per_s + nsec;
}

/// Clock provides high-resolution time measurements.
pub const Clock = struct {
    started: u64 = 0,

    pub fn init() Clock {
        return .{
            .started = nanoTime(),
        };
    }

    /// Seconds since clock was started.
    pub fn elapsed(self: Clock) f64 {
        const now = nanoTime();
        const elapsed_ns = now - self.started;
        return @as(f64, @floatFromInt(elapsed_ns)) / @as(f64, std.time.ns_per_s);
    }

    /// Nanoseconds since clock was started.
    pub fn elapsedNs(self: Clock) u64 {
        const now = nanoTime();
        return now - self.started;
    }
};

/// Delta time tracker for game loop.
pub const DeltaTime = struct {
    last_frame: u64 = 0,
    frequency: u64,

    pub fn init() DeltaTime {
        return .{
            .frequency = std.time.ns_per_s,
            .last_frame = nanoTime(),
        };
    }

    /// Call once per frame. Returns delta time in seconds.
    pub fn tick(self: *DeltaTime) f64 {
        const now = nanoTime();
        const elapsed = now - self.last_frame;
        self.last_frame = now;
        return @as(f64, @floatFromInt(elapsed)) / @as(f64, @floatFromInt(self.frequency));
    }
};

/// Frame rate counter.
pub const FpsCounter = struct {
    frame_count: u32 = 0,
    elapsed: f64 = 0,
    current_fps: u32 = 0,
    last_tick: f64 = 0,

    pub fn init() FpsCounter {
        return .{};
    }

    /// Call every frame. Returns current FPS (updated once per second).
    pub fn tick(self: *FpsCounter, dt: f64) u32 {
        self.frame_count += 1;
        self.elapsed += dt;

        if (self.elapsed >= 1.0) {
            self.current_fps = self.frame_count;
            self.frame_count = 0;
            self.elapsed -= 1.0;
        }

        return self.current_fps;
    }

    /// Get the most recently computed FPS.
    pub fn fps(self: FpsCounter) u32 {
        return self.current_fps;
    }
};

/// Simple timer for cooldowns and intervals.
pub const Timer = struct {
    duration: f64,
    elapsed: f64 = 0,
    finished: bool = false,

    pub fn init(duration: f64) Timer {
        return .{ .duration = duration };
    }

    /// Advance the timer by dt. Returns true if timer just finished.
    pub fn update(self: *Timer, dt: f64) bool {
        if (self.finished) return false;
        self.elapsed += dt;
        if (self.elapsed >= self.duration) {
            self.finished = true;
            return true;
        }
        return false;
    }

    /// Reset the timer.
    pub fn reset(self: *Timer) void {
        self.elapsed = 0;
        self.finished = false;
    }

    /// Progress as a 0..1 value.
    pub fn progress(self: Timer) f64 {
        if (self.duration <= 0) return 1.0;
        return @min(1.0, self.elapsed / self.duration);
    }
};

test "clock measures elapsed time" {
    var clock = Clock.init();
    const elapsed_before = clock.elapsed();
    // Busy wait for ~1ms
    const target = nanoTime() + std.time.ns_per_ms;
    while (nanoTime() < target) {}
    const elapsed_after = clock.elapsed();
    try std.testing.expect(elapsed_after >= elapsed_before);
}

test "delta time increases" {
    var dt = DeltaTime.init();
    const first = dt.tick();
    _ = first;
    const target = nanoTime() + @as(u64, 1_000_000); // 1ms
    while (nanoTime() < target) {}
    const second = dt.tick();
    try std.testing.expect(second >= 0);
}

test "fps counter accumulates" {
    var fps = FpsCounter.init();
    const result = fps.tick(0.016); // ~60 FPS frame time
    try std.testing.expectEqual(@as(u32, 0), result); // Still accumulating
}

test "timer finishes after duration" {
    var timer = Timer.init(1.0);
    try std.testing.expect(!timer.finished);
    _ = timer.update(1.5);
    try std.testing.expect(timer.finished);
}

test "timer progress" {
    var timer = Timer.init(2.0);
    _ = timer.update(1.0);
    try std.testing.expectApproxEqAbs(@as(f64, 0.5), timer.progress(), 0.001);
    _ = timer.update(1.0);
    try std.testing.expectApproxEqAbs(@as(f64, 1.0), timer.progress(), 0.001);
}

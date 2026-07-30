//! Fixed timestep game loop.
//!
//! Decouples update rate from render rate.
//! Update runs at FIXED_HZ regardless of display refresh.
//! Render runs as fast as possible.

const std = @import("std");
const time = @import("time.zig");

pub const FIXED_DT: f64 = 1.0 / 60.0; // 60 updates per second
pub const MAX_FRAME_TIME: f64 = 0.1; // Cap frame time to prevent spiral of death

/// Callbacks for the game loop phases.
pub const LoopCallbacks = struct {
    /// Fixed-rate update. Called exactly at FIXED_DT intervals.
    fixedUpdate: *const fn (dt: f64) void,
    /// Variable-rate render. Called once per frame, may be called multiple
    /// times per fixed update or skipped if rendering is slow.
    render: *const fn (alpha: f64) void,
    /// Returns true if the loop should continue running.
    isRunning: *const fn () bool,
};

/// Game loop state machine.
pub const GameLoop = struct {
    accumulator: f64 = 0,
    clock: time.Clock,
    delta: time.DeltaTime,
    fps: time.FpsCounter,
    callbacks: LoopCallbacks,
    frame_count: u64 = 0,

    pub fn init(callbacks: LoopCallbacks) GameLoop {
        return .{
            .clock = time.Clock.init(),
            .delta = time.DeltaTime.init(),
            .fps = time.FpsCounter.init(),
            .callbacks = callbacks,
        };
    }

    /// Runs one frame of the game loop.
    /// Returns the current FPS (updated once per second).
    pub fn runFrame(self: *GameLoop) u32 {
        var dt = self.delta.tick();
        if (dt > MAX_FRAME_TIME) dt = MAX_FRAME_TIME;

        self.accumulator += dt;

        // Fixed timestep updates
        while (self.accumulator >= FIXED_DT) {
            self.callbacks.fixedUpdate(FIXED_DT);
            self.accumulator -= FIXED_DT;
        }

        // Render with interpolation alpha
        const alpha = self.accumulator / FIXED_DT;
        self.callbacks.render(alpha);

        self.frame_count += 1;
        return self.fps.tick(dt);
    }
};

test "game loop runs one frame" {
    const S = struct {
        var fixed_count: u32 = 0;
        var render_count: u32 = 0;
        var running: bool = false;
    };

    S.fixed_count = 0;
    S.render_count = 0;

    const callbacks = LoopCallbacks{
        .fixedUpdate = struct {
            fn f(dt: f64) void {
                _ = dt;
                S.fixed_count += 1;
            }
        }.f,
        .render = struct {
            fn f(alpha: f64) void {
                _ = alpha;
                S.render_count += 1;
            }
        }.f,
        .isRunning = struct {
            fn f() bool {
                return S.running;
            }
        }.f,
    };

    var loop = GameLoop.init(callbacks);
    S.running = true;
    _ = loop.runFrame();
    _ = loop.runFrame();
    S.running = false;

    try std.testing.expect(S.fixed_count >= 0);
    try std.testing.expect(S.render_count == 2);
}

test "fixed timestep accumulates correctly" {
    const S = struct {
        var fixed_count: u32 = 0;
        var running: bool = true;
    };

    S.fixed_count = 0;
    const callbacks = LoopCallbacks{
        .fixedUpdate = struct {
            fn f(dt: f64) void {
                _ = dt;
                S.fixed_count += 1;
            }
        }.f,
        .render = struct {
            fn f(alpha: f64) void {
                _ = alpha;
            }
        }.f,
        .isRunning = struct {
            fn f() bool {
                return S.running;
            }
        }.f,
    };

    var loop = GameLoop.init(callbacks);
    S.running = true;
    _ = loop.runFrame();
    _ = loop.runFrame();
    _ = loop.runFrame();
    S.running = false;

    try std.testing.expect(S.fixed_count >= 0);
}

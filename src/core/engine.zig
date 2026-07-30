//! Infinity Engine core lifecycle.
//!
//! Engine is the top-level orchestrator.
//! init() → run() → shutdown() is the canonical lifecycle.

const std = @import("std");
const loop_mod = @import("loop.zig");
const memory_mod = @import("memory.zig");
const ecs = @import("../ecs/world.zig");
const platform = @import("../platform/window.zig");
const renderer_mod = @import("../renderer/renderer.zig");
const input_mod = @import("../platform/input.zig");
const Error = @import("error.zig").Error;

/// Engine configuration.
pub const Config = struct {
    window_title: [:0]const u8 = "Infinity Engine",
    window_width: u32 = 1280,
    window_height: u32 = 720,
    frame_memory_size: usize = 1024 * 1024 * 2, // 2MB per frame
    max_entities: u32 = 10000,
};

/// Engine state machine.
pub const Engine = struct {
    allocator: std.mem.Allocator,
    config: Config,
    window: platform.Window,
    renderer: renderer_mod.Renderer,
    world: ecs.World,
    input: input_mod.InputState,
    frame_allocator: memory_mod.FrameAllocator,
    running: bool = false,

    pub fn init(allocator: std.mem.Allocator, config: Config) !Engine {
        var self: Engine = undefined;
        self.allocator = allocator;
        self.config = config;
        self.window = try platform.Window.init(
            allocator,
            config.window_title,
            config.window_width,
            config.window_height,
        );
        errdefer self.window.deinit();
        self.renderer = try renderer_mod.Renderer.init(allocator, config.window_width, config.window_height);
        self.world = try ecs.World.init(allocator, config.max_entities);
        self.input = input_mod.InputState.init();
        self.frame_allocator = try memory_mod.FrameAllocator.init(config.frame_memory_size);
        self.running = false;
        return self;
    }

    pub fn deinit(self: *Engine) void {
        self.frame_allocator.deinit();
        self.renderer.deinit();
        self.window.deinit();
    }

    /// Returns a frame-scoped allocator (reset every frame).
    pub fn frameAllocator(self: *Engine) std.mem.Allocator {
        return self.frame_allocator.allocator();
    }

    /// Begin a new frame: swap frame buffers and reset frame allocator.
    pub fn beginFrame(self: *Engine) void {
        self.frame_allocator.beginFrame();
    }

    /// Process platform events (input, window messages).
    pub fn pollEvents(self: *Engine) void {
        self.window.pollEvents(&self.input);
    }

    /// Check if engine is still running.
    pub fn isRunning(self: *Engine) bool {
        return self.running and !self.window.shouldClose();
    }
};

test "engine init and deinit" {
    const allocator = std.testing.allocator;
    var engine = try Engine.init(allocator, .{});
    defer engine.deinit();
    try std.testing.expect(!engine.running);
}

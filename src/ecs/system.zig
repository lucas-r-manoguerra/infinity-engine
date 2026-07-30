//! System interface for ECS.
//!
//! Systems are the "S" in ECS: they contain the logic that iterates over
//! entities with specific component combinations.

const std = @import("std");
const Entity = @import("entity.zig").Entity;
const World = @import("world.zig").World;

/// Phase in the game loop when a system runs.
pub const SystemPhase = enum {
    pre_update,
    update,
    post_update,
    render,
};

/// A system is a function that operates on the world.
/// Systems can be simple function pointers or structs with state.
pub const System = struct {
    name: []const u8,
    phase: SystemPhase,
    context: ?*anyopaque,
    run: *const fn (ctx: ?*anyopaque, world: *World, dt: f64) void,

    pub fn init(name: []const u8, phase: SystemPhase, context: ?*anyopaque, run: *const fn (ctx: ?*anyopaque, world: *World, dt: f64) void) System {
        return .{ .name = name, .phase = phase, .context = context, .run = run };
    }
};

/// Collection of systems organized by phase.
pub const SystemGraph = struct {
    pre_update: std.ArrayList(System),
    update: std.ArrayList(System),
    post_update: std.ArrayList(System),
    render: std.ArrayList(System),

    pub fn init() SystemGraph {
        return .{
            .pre_update = .{ .items = &.{}, .capacity = 0 },
            .update = .{ .items = &.{}, .capacity = 0 },
            .post_update = .{ .items = &.{}, .capacity = 0 },
            .render = .{ .items = &.{}, .capacity = 0 },
        };
    }

    pub fn deinit(self: *SystemGraph, allocator: std.mem.Allocator) void {
        self.pre_update.deinit(allocator);
        self.update.deinit(allocator);
        self.post_update.deinit(allocator);
        self.render.deinit(allocator);
    }

    /// Add a system to its phase.
    pub fn add(self: *SystemGraph, allocator: std.mem.Allocator, system: System) !void {
        const list = switch (system.phase) {
            .pre_update => &self.pre_update,
            .update => &self.update,
            .post_update => &self.post_update,
            .render => &self.render,
        };
        try list.append(allocator, system);
    }

    /// Run all systems for a given phase.
    pub fn runPhase(self: SystemGraph, world: *World, phase: SystemPhase, dt: f64) void {
        const list = switch (phase) {
            .pre_update => &self.pre_update,
            .update => &self.update,
            .post_update => &self.post_update,
            .render => &self.render,
        };
        for (list.items) |system| {
            system.run(system.context, world, dt);
        }
    }
};

test "system creation and phase" {
    const S = struct {
        var ran: bool = false;
    };
    S.ran = false;

    const sys = System.init("test", .update, null, struct {
        fn run(_: ?*anyopaque, _: *World, _: f64) void {
            S.ran = true;
        }
    }.run);

    try std.testing.expectEqualStrings("test", sys.name);
    try std.testing.expectEqual(.update, sys.phase);
}

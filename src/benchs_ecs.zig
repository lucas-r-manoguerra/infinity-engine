//! Benchmarks: ecs/ (World, Entity lifecycle, Query iteration)
//!
//! Each benchmark separates setup/teardown from the timed operation so
//! measurements reflect actual ECS performance, not allocator setup noise.
//!
//! Run via: `zig build bench`

const std = @import("std");
const time = @import("core/time.zig");
const World = @import("ecs/world.zig").World;
const Entity = @import("ecs/entity.zig").Entity;
const Query = @import("ecs/query.zig").Query;

const ITERATIONS = 100_000;
const ENTITY_COUNT = 10_000;

const allocator = std.heap.page_allocator;

const Position = struct { x: f32, y: f32, z: f32 };
const Velocity = struct { x: f32, y: f32, z: f32 };

// ---------------------------------------------------------------------------
// Entity creation  (measures world.createEntity only)
// ---------------------------------------------------------------------------

fn benchCreateEntities() void {
    var world = World.init(allocator, ITERATIONS) catch unreachable;
    defer world.deinit();

    const entities = allocator.alloc(Entity, ITERATIONS) catch unreachable;
    defer allocator.free(entities);

    // warmup — destroy it so it doesn't steal a slot from the timed loop
    {
        const warmup = world.createEntity() catch unreachable;
        world.destroyEntity(warmup);
    }

    const start = time.nanoTime();
    for (0..ITERATIONS) |i| {
        entities[i] = world.createEntity() catch unreachable;
    }
    const elapsed = time.nanoTime() - start;

    for (entities) |e| world.destroyEntity(e);

    report("entity.create", elapsed);
}

// ---------------------------------------------------------------------------
// Entity destruction  (measures world.destroyEntity only)
// ---------------------------------------------------------------------------

fn benchDestroyEntities() void {
    var world = World.init(allocator, ITERATIONS) catch unreachable;
    defer world.deinit();

    const entities = allocator.alloc(Entity, ITERATIONS) catch unreachable;
    defer allocator.free(entities);

    for (0..ITERATIONS) |i| {
        entities[i] = world.createEntity() catch unreachable;
    }

    // warmup
    world.destroyEntity(entities[0]);

    const start = time.nanoTime();
    for (entities) |e| world.destroyEntity(e);
    const elapsed = time.nanoTime() - start;

    report("entity.destroy", elapsed);
}

// ---------------------------------------------------------------------------
// Create + destroy  (measures the pair as one op)
// ---------------------------------------------------------------------------

fn benchCreateAndDestroy() void {
    var world = World.init(allocator, 1) catch unreachable;
    defer world.deinit();

    // warmup — create + destroy so the slot is free for timed loop
    {
        const e = world.createEntity() catch unreachable;
        world.destroyEntity(e);
    }

    const start = time.nanoTime();
    for (0..ITERATIONS) |_| {
        const e = world.createEntity() catch unreachable;
        world.destroyEntity(e);
    }
    const elapsed = time.nanoTime() - start;

    report("entity.create+destroy", elapsed);
}

// ---------------------------------------------------------------------------
// Component add
// ---------------------------------------------------------------------------

fn benchAddComponent() void {
    var world = World.init(allocator, ITERATIONS) catch unreachable;
    defer world.deinit();

    const pos_id = world.registerComponent(Position) catch unreachable;

    const entities = allocator.alloc(Entity, ITERATIONS) catch unreachable;
    defer allocator.free(entities);

    for (0..ITERATIONS) |i| {
        entities[i] = world.createEntity() catch unreachable;
    }

    // warmup
    world.addComponent(entities[0], pos_id, Position, .{ .x = 1, .y = 2, .z = 3 }) catch unreachable;
    _ = world.removeComponent(entities[0], pos_id, Position);

    const start = time.nanoTime();
    for (entities) |e| {
        world.addComponent(e, pos_id, Position, .{ .x = 1, .y = 2, .z = 3 }) catch unreachable;
    }
    const elapsed = time.nanoTime() - start;

    for (entities) |e| {
        _ = world.removeComponent(e, pos_id, Position);
        world.destroyEntity(e);
    }

    report("component.add", elapsed);
}

// ---------------------------------------------------------------------------
// Component get
// ---------------------------------------------------------------------------

fn benchGetComponent() void {
    var world = World.init(allocator, ITERATIONS) catch unreachable;
    defer world.deinit();

    const pos_id = world.registerComponent(Position) catch unreachable;

    const entities = allocator.alloc(Entity, ITERATIONS) catch unreachable;
    defer allocator.free(entities);

    for (0..ITERATIONS) |i| {
        entities[i] = world.createEntity() catch unreachable;
    }

    for (entities) |e| {
        world.addComponent(e, pos_id, Position, .{ .x = 1, .y = 2, .z = 3 }) catch unreachable;
    }

    // warmup
    _ = world.getComponent(entities[0], pos_id, Position);

    const start = time.nanoTime();
    for (entities) |e| {
        _ = world.getComponent(e, pos_id, Position);
    }
    const elapsed = time.nanoTime() - start;

    for (entities) |e| {
        _ = world.removeComponent(e, pos_id, Position);
        world.destroyEntity(e);
    }

    report("component.get", elapsed);
}

// ---------------------------------------------------------------------------
// Component remove
// ---------------------------------------------------------------------------

fn benchRemoveComponent() void {
    var world = World.init(allocator, ITERATIONS) catch unreachable;
    defer world.deinit();

    const pos_id = world.registerComponent(Position) catch unreachable;

    const entities = allocator.alloc(Entity, ITERATIONS) catch unreachable;
    defer allocator.free(entities);

    for (0..ITERATIONS) |i| {
        entities[i] = world.createEntity() catch unreachable;
    }

    for (entities) |e| {
        world.addComponent(e, pos_id, Position, .{ .x = 1, .y = 2, .z = 3 }) catch unreachable;
    }

    // warmup
    _ = world.removeComponent(entities[0], pos_id, Position);
    world.addComponent(entities[0], pos_id, Position, .{ .x = 1, .y = 2, .z = 3 }) catch unreachable;

    const start = time.nanoTime();
    for (entities) |e| {
        _ = world.removeComponent(e, pos_id, Position);
    }
    const elapsed = time.nanoTime() - start;

    for (entities) |e| world.destroyEntity(e);

    report("component.remove", elapsed);
}

// ---------------------------------------------------------------------------
// Query iteration  (10k entities with Position + Velocity)
// ---------------------------------------------------------------------------

fn benchQueryIteration() void {
    var world = World.init(allocator, ENTITY_COUNT) catch unreachable;
    defer world.deinit();

    const pos_id = world.registerComponent(Position) catch unreachable;
    const vel_id = world.registerComponent(Velocity) catch unreachable;

    for (0..ENTITY_COUNT) |_| {
        const e = world.createEntity() catch unreachable;
        world.addComponent(e, pos_id, Position, .{ .x = 1, .y = 2, .z = 3 }) catch unreachable;
        world.addComponent(e, vel_id, Velocity, .{ .x = 1, .y = 0, .z = 0 }) catch unreachable;
    }

    // warmup
    {
        var q = Query.init(&world, &.{ pos_id, vel_id });
        while (q.next()) |_| {}
    }

    // time N iterations of full query passes
    const QUERY_PASSES = 10_000;
    const start = time.nanoTime();
    for (0..QUERY_PASSES) |_| {
        var q = Query.init(&world, &.{ pos_id, vel_id });
        while (q.next()) |_| {}
    }
    const elapsed = time.nanoTime() - start;

    reportF("query.iterate (10k entities)", elapsed, QUERY_PASSES);
}

// ---------------------------------------------------------------------------
// Query iteration (empty world)
// ---------------------------------------------------------------------------

fn benchQueryIterationEmpty() void {
    var world = World.init(allocator, ENTITY_COUNT) catch unreachable;
    defer world.deinit();

    const pos_id = world.registerComponent(Position) catch unreachable;

    // warmup
    {
        var q = Query.init(&world, &.{pos_id});
        while (q.next()) |_| {}
    }

    const QUERY_PASSES = 100_000;
    const start = time.nanoTime();
    for (0..QUERY_PASSES) |_| {
        var q = Query.init(&world, &.{pos_id});
        while (q.next()) |_| {}
    }
    const elapsed = time.nanoTime() - start;

    reportF("query.iterate (empty)", elapsed, QUERY_PASSES);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn report(comptime name: []const u8, elapsed_ns: u64) void {
    const el_f: f64 = @as(f64, @floatFromInt(elapsed_ns));
    const it_f: f64 = @as(f64, @floatFromInt(ITERATIONS));
    std.debug.print("  {s: <38}  {d: >8.1} ns/op\n", .{ name, el_f / it_f });
}

fn reportF(comptime name: []const u8, elapsed_ns: u64, iters: u32) void {
    const el_f: f64 = @as(f64, @floatFromInt(elapsed_ns));
    const it_f: f64 = @as(f64, @floatFromInt(iters));
    std.debug.print("  {s: <38}  {d: >8.1} ns/op\n", .{ name, el_f / it_f });
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

pub fn run() void {
    std.debug.print("\n  ~ ecs benchmarks (ITERATIONS={d}) ~\n", .{ITERATIONS});

    benchCreateEntities();
    benchDestroyEntities();
    benchCreateAndDestroy();
    benchAddComponent();
    benchGetComponent();
    benchRemoveComponent();
    benchQueryIteration();
    benchQueryIterationEmpty();
}

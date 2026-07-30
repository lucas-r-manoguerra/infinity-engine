//! ECS query for iterating entities with specific components.

const std = @import("std");
const Entity = @import("entity.zig").Entity;
const World = @import("world.zig").World;
const ComponentId = @import("component.zig").ComponentId;

/// Runtime query that iterates entities with ALL specified components.
pub const Query = struct {
    world: *World,
    component_ids: []const ComponentId,
    current_index: u32,

    /// Initialize a query for entities matching all given component IDs.
    pub fn init(world: *World, component_ids: []const ComponentId) Query {
        return .{
            .world = world,
            .component_ids = component_ids,
            .current_index = 0,
        };
    }

    /// Return the next matching entity, or null.
    pub fn next(self: *Query) ?Entity {
        while (self.current_index < self.world.max_entities) : (self.current_index += 1) {
            const idx = self.current_index;
            if (idx >= self.world.entity_pool.items.len) continue;

            const stored = self.world.entity_pool.items[idx];
            if (!stored.isValid()) continue;

            const entity = Entity.init(idx, self.world.generations.items[idx]);
            if (!self.world.isAlive(entity)) continue;

            var all_have = true;
            for (self.component_ids) |cid| {
                if (!self.world.hasComponent(entity, cid)) {
                    all_have = false;
                    break;
                }
            }
            if (all_have) {
                self.current_index += 1;
                return entity;
            }
        }
        return null;
    }
};

test "query basic iteration" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 100);
    defer world.deinit();

    const Pos = struct { x: f32, y: f32 };
    const Vel = struct { x: f32, y: f32 };

    const pos_id = try world.registerComponent(Pos);
    const vel_id = try world.registerComponent(Vel);

    const e1 = try world.createEntity();
    try world.addComponent(e1, pos_id, Pos, .{ .x = 0, .y = 0 });

    const e2 = try world.createEntity();
    try world.addComponent(e2, pos_id, Pos, .{ .x = 1, .y = 2 });
    try world.addComponent(e2, vel_id, Vel, .{ .x = 0.5, .y = 0.1 });

    const e3 = try world.createEntity();
    try world.addComponent(e3, pos_id, Pos, .{ .x = 3, .y = 4 });
    try world.addComponent(e3, vel_id, Vel, .{ .x = 0.2, .y = 0.3 });

    var query = Query.init(&world, &[_]ComponentId{ pos_id, vel_id });
    const first = query.next().?;
    const second = query.next().?;
    const third = query.next();

    try std.testing.expect(Entity.eql(first, e2));
    try std.testing.expect(Entity.eql(second, e3));
    try std.testing.expect(third == null);
}

test "query returns nothing for no match" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 10);
    defer world.deinit();

    const A = struct { v: u32 };
    const B = struct { v: u32 };

    const a_id = try world.registerComponent(A);
    const b_id = try world.registerComponent(B);

    const e1 = try world.createEntity();
    try world.addComponent(e1, a_id, A, .{ .v = 1 });

    var query = Query.init(&world, &[_]ComponentId{b_id});
    try std.testing.expect(query.next() == null);
}

test "query destroy resets query position" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 10);
    defer world.deinit();

    const C = struct { v: u32 };
    const c_id = try world.registerComponent(C);

    // Create and destroy an entity to bump the index past the destroyed entity
    const e1 = try world.createEntity();
    try world.addComponent(e1, c_id, C, .{ .v = 1 });
    world.destroyEntity(e1);

    const e2 = try world.createEntity();
    try world.addComponent(e2, c_id, C, .{ .v = 2 });

    var query = Query.init(&world, &[_]ComponentId{c_id});
    const first = query.next().?;
    try std.testing.expect(Entity.eql(first, e2));
    try std.testing.expect(query.next() == null);
}

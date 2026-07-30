//! ECS World: owns entities and components.
//!
//! The World is the top-level ECS container. It manages:
//! - Entity creation and destruction
//! - Component registration and storage
//! - System execution

const std = @import("std");
const Entity = @import("entity.zig").Entity;
const ComponentReg = @import("component.zig");
const SystemMod = @import("system.zig");
const Error = error{
    OutOfMemory,
    NotFound,
    CapacityReached,
    NotImplemented,
    InvalidArgument,
    IoError,
};

/// Type-erased component storage pointer.
const StoragePtr = struct {
    ptr: *anyopaque,
    vtable: *const StorageVTable,

    const StorageVTable = struct {
        deinit: *const fn (ptr: *anyopaque, allocator: std.mem.Allocator) void,
        has: *const fn (ptr: *anyopaque, entity: Entity) bool,
        remove: *const fn (ptr: *anyopaque, allocator: std.mem.Allocator, entity: Entity) bool,
    };
};

/// The ECS World.
pub const World = struct {
    allocator: std.mem.Allocator,
    max_entities: u32,

    /// Entity management
    entity_pool: std.ArrayList(Entity),
    generations: std.ArrayList(u32),
    free_indices: std.ArrayList(u32),

    /// Component registry
    component_registry: ComponentReg.ComponentRegistry,
    component_storages: std.ArrayList(StoragePtr),

    /// Systems
    systems: SystemMod.SystemGraph,

    pub fn init(allocator: std.mem.Allocator, max_entities: u32) !World {
        return .{
            .allocator = allocator,
            .max_entities = max_entities,
            .entity_pool = try std.ArrayListUnmanaged(Entity).initCapacity(allocator, max_entities),
            .generations = try std.ArrayListUnmanaged(u32).initCapacity(allocator, max_entities),
            .free_indices = .{ .items = &.{}, .capacity = 0 },
            .component_registry = ComponentReg.ComponentRegistry.init(64),
            .component_storages = .{ .items = &.{}, .capacity = 0 },
            .systems = SystemMod.SystemGraph.init(),
        };
    }

    pub fn deinit(self: *World) void {
        for (self.component_storages.items) |*storage| {
            storage.vtable.deinit(storage.ptr, self.allocator);
        }
        self.component_storages.deinit(self.allocator);
        self.entity_pool.deinit(self.allocator);
        self.generations.deinit(self.allocator);
        self.free_indices.deinit(self.allocator);
        self.systems.deinit(self.allocator);
    }

    /// Create a new entity. Returns the entity handle.
    pub fn createEntity(self: *World) Error!Entity {
        if (self.free_indices.items.len > 0) {
            const index = self.free_indices.pop().?;
            const generation = self.generations.items[index] + 1;
            const entity = Entity.init(index, generation);
            self.entity_pool.items[index] = entity;
            self.generations.items[index] = generation;
            return entity;
        }

        if (self.entity_pool.items.len >= self.max_entities) {
            return Error.CapacityReached;
        }

        const index = @as(u32, @intCast(self.entity_pool.items.len));
        const entity = Entity.init(index, 1);
        try self.entity_pool.append(self.allocator, entity);
        try self.generations.append(self.allocator, 1);
        return entity;
    }

    /// Destroy an entity and all its components.
    pub fn destroyEntity(self: *World, entity: Entity) void {
        if (!self.isAlive(entity)) return;

        // Remove all components
        for (self.component_storages.items) |*storage| {
            _ = storage.vtable.remove(storage.ptr, self.allocator, entity);
        }

        // Mark entity as dead
        self.entity_pool.items[entity.index] = Entity.INVALID;
        self.free_indices.append(self.allocator, entity.index) catch {};
    }

    /// Returns the number of alive entities.
    pub fn aliveCount(self: World) u32 {
        return @as(u32, @intCast(self.entity_pool.items.len - self.free_indices.items.len));
    }

    /// Check if an entity is alive.
    pub fn isAlive(self: World, entity: Entity) bool {
        if (!entity.isValid()) return false;
        if (entity.index >= self.entity_pool.items.len) return false;
        const stored = self.entity_pool.items[entity.index];
        return Entity.eql(stored, entity);
    }

    /// Register a component type. Returns the component ID.
    pub fn registerComponent(self: *World, comptime T: type) Error!ComponentReg.ComponentId {
        const id = try self.component_registry.register();

        // Create the storage
        const storage = try ComponentReg.ComponentStorage(T).init(self.allocator, self.max_entities);

        const ptr = try self.allocator.create(ComponentReg.ComponentStorage(T));
        ptr.* = storage;

        try self.component_storages.append(self.allocator, .{
            .ptr = ptr,
            .vtable = &.{
                .deinit = struct {
                    fn f(p: *anyopaque, alloc: std.mem.Allocator) void {
                        const s: *ComponentReg.ComponentStorage(T) = @ptrCast(@alignCast(p));
                        s.deinit(alloc);
                        alloc.destroy(s);
                    }
                }.f,
                .has = struct {
                    fn f(p: *anyopaque, entity: Entity) bool {
                        const s: *ComponentReg.ComponentStorage(T) = @ptrCast(@alignCast(p));
                        return s.has(entity);
                    }
                }.f,
                .remove = struct {
                    fn f(p: *anyopaque, alloc: std.mem.Allocator, entity: Entity) bool {
                        const s: *ComponentReg.ComponentStorage(T) = @ptrCast(@alignCast(p));
                        return s.remove(alloc, entity);
                    }
                }.f,
            },
        });

        return id;
    }

    /// Add a component to an entity by component ID.
    pub fn addComponent(self: *World, entity: Entity, component_id: ComponentReg.ComponentId, comptime T: type, component: T) !void {
        const storage_ptr = &self.component_storages.items[@as(usize, component_id)];
        const storage: *ComponentReg.ComponentStorage(T) = @ptrCast(@alignCast(storage_ptr.ptr));
        try storage.add(self.allocator, entity, component);
    }

    /// Get a component from an entity by component ID.
    pub fn getComponent(self: *World, entity: Entity, component_id: ComponentReg.ComponentId, comptime T: type) ?*T {
        const storage_ptr = &self.component_storages.items[@as(usize, component_id)];
        const storage: *ComponentReg.ComponentStorage(T) = @ptrCast(@alignCast(storage_ptr.ptr));
        return storage.get(entity);
    }

    /// Remove a component from an entity by component ID.
    pub fn removeComponent(self: *World, entity: Entity, component_id: ComponentReg.ComponentId, comptime T: type) bool {
        const storage_ptr = &self.component_storages.items[@as(usize, component_id)];
        const storage: *ComponentReg.ComponentStorage(T) = @ptrCast(@alignCast(storage_ptr.ptr));
        return storage.remove(self.allocator, entity);
    }

    /// Check if an entity has a component by component ID.
    pub fn hasComponent(self: *World, entity: Entity, component_id: ComponentReg.ComponentId) bool {
        if (@as(usize, component_id) >= self.component_storages.items.len) return false;
        const storage_ptr = self.component_storages.items[@as(usize, component_id)];
        return storage_ptr.vtable.has(storage_ptr.ptr, entity);
    }

    /// Add a system to the world.
    pub fn addSystem(self: *World, system: SystemMod.System) !void {
        try self.systems.add(self.allocator, system);
    }

    /// Run all systems for a phase.
    pub fn runSystems(self: *World, phase: SystemMod.SystemPhase, dt: f64) void {
        self.systems.runPhase(self, phase, dt);
    }
};

test "world create entity" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 100);
    defer world.deinit();

    const entity = try world.createEntity();
    try std.testing.expect(entity.isValid());
    try std.testing.expect(world.isAlive(entity));
}

test "world destroy entity" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 100);
    defer world.deinit();

    const entity = try world.createEntity();
    try std.testing.expect(world.isAlive(entity));

    world.destroyEntity(entity);
    try std.testing.expect(!world.isAlive(entity));
}

test "world max entities" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 5);
    defer world.deinit();

    var i: u32 = 0;
    while (i < 5) : (i += 1) {
        _ = try world.createEntity();
    }

    // 6th should fail
    try std.testing.expectError(Error.CapacityReached, world.createEntity());
}

test "world reuse entity index after destroy" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 10);
    defer world.deinit();

    const e1 = try world.createEntity();
    const index1 = e1.index;
    world.destroyEntity(e1);

    const e2 = try world.createEntity();
    try std.testing.expectEqual(index1, e2.index);
    try std.testing.expect(e2.generation > e1.generation);
}

test "world register component and add" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 10);
    defer world.deinit();

    const Comp = struct { value: u32 };
    const id = try world.registerComponent(Comp);

    const entity = try world.createEntity();
    try world.addComponent(entity, id, Comp, .{ .value = 42 });
    try std.testing.expect(world.hasComponent(entity, id));
}

test "world get component by id" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 10);
    defer world.deinit();

    const Comp = struct { value: f32 };
    const id = try world.registerComponent(Comp);

    const entity = try world.createEntity();
    try world.addComponent(entity, id, Comp, .{ .value = 3.14 });

    const comp = world.getComponent(entity, id, Comp);
    try std.testing.expect(comp != null);
    try std.testing.expectApproxEqAbs(@as(f32, 3.14), comp.?.value, 0.001);
}

test "world remove component by id" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 10);
    defer world.deinit();

    const Comp = struct { value: u32 };
    const id = try world.registerComponent(Comp);

    const entity = try world.createEntity();
    try world.addComponent(entity, id, Comp, .{ .value = 99 });
    try std.testing.expect(world.hasComponent(entity, id));

    _ = world.removeComponent(entity, id, Comp);
    try std.testing.expect(!world.hasComponent(entity, id));
}

test "world get component returns null for missing" {
    const allocator = std.testing.allocator;
    var world = try World.init(allocator, 10);
    defer world.deinit();

    const Comp = struct { value: u32 };
    const id = try world.registerComponent(Comp);

    const entity = try world.createEntity();
    // entity has no component
    const comp = world.getComponent(entity, id, Comp);
    try std.testing.expect(comp == null);
}

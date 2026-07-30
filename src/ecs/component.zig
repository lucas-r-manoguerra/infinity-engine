//! Component type registry and storage.
//!
//! Components are plain data attached to entities.
//! Each component type gets a unique ID and a sparse set for storage.

const std = @import("std");
const Entity = @import("entity.zig").Entity;
const Error = error{
    NotFound,
    CapacityReached,
    NotImplemented,
    InvalidArgument,
    IoError,
};

/// Unique identifier for a component type.
pub const ComponentId = u16;

/// Component registry: maps type info to ComponentIds.
pub const ComponentRegistry = struct {
    next_id: ComponentId = 0,
    max_components: ComponentId,

    pub fn init(max_components: ComponentId) ComponentRegistry {
        return .{ .max_components = max_components };
    }

    /// Register a component type and get its ID.
    /// Returns error if max components reached.
    pub fn register(self: *ComponentRegistry) Error!ComponentId {
        if (self.next_id >= self.max_components) return Error.CapacityReached;
        const id = self.next_id;
        self.next_id += 1;
        return id;
    }

    /// Number of registered component types.
    pub fn count(self: ComponentRegistry) usize {
        return self.next_id;
    }
};

/// Sparse set storage for a single component type.
/// Provides O(1) add/remove/get and cache-friendly iteration.
pub fn ComponentStorage(comptime T: type) type {
    return struct {
        const Self = @This();

        /// Dense array: components stored contiguously.
        dense: std.ArrayListUnmanaged(T),
        /// Sparse array: entity index -> position in dense array.
        sparse: std.ArrayListUnmanaged(u32),
        /// Entity generation for each sparse entry.
        generations: std.ArrayListUnmanaged(u32),
        /// Map: dense position -> entity.
        entities: std.ArrayListUnmanaged(Entity),

        /// Initialize storage with capacity.
        pub fn init(allocator: std.mem.Allocator, capacity: u32) !Self {
            return .{
                .dense = try std.ArrayListUnmanaged(T).initCapacity(allocator, capacity),
                .sparse = try std.ArrayListUnmanaged(u32).initCapacity(allocator, capacity),
                .generations = try std.ArrayListUnmanaged(u32).initCapacity(allocator, capacity),
                .entities = try std.ArrayListUnmanaged(Entity).initCapacity(allocator, capacity),
            };
        }

        pub fn deinit(self: *Self, allocator: std.mem.Allocator) void {
            self.dense.deinit(allocator);
            self.sparse.deinit(allocator);
            self.generations.deinit(allocator);
            self.entities.deinit(allocator);
        }

        /// Ensure sparse arrays are large enough for the entity index.
        fn ensureSparse(self: *Self, allocator: std.mem.Allocator, entity_index: u32) !void {
            const needed = @as(usize, entity_index) + 1;
            const old_len = self.sparse.items.len;
            if (needed > old_len) {
                try self.sparse.resize(allocator, needed);
                try self.generations.resize(allocator, needed);
                @memset(self.sparse.items[old_len..], std.math.maxInt(u32));
                @memset(self.generations.items[old_len..], 0);
            }
        }

        /// Add a component to an entity.
        pub fn add(self: *Self, allocator: std.mem.Allocator, entity: Entity, component: T) !void {
            try self.ensureSparse(allocator, entity.index);
            const dense_pos = self.dense.items.len;
            try self.dense.append(allocator, component);
            try self.entities.append(allocator, entity);
            self.sparse.items[entity.index] = @intCast(dense_pos);
            self.generations.items[entity.index] = entity.generation;
        }

        /// Get a component for an entity. Returns null if entity doesn't have this component.
        pub fn get(self: Self, entity: Entity) ?*T {
            if (entity.index >= self.sparse.items.len) return null;
            if (self.generations.items[entity.index] != entity.generation) return null;
            const dense_pos = self.sparse.items[entity.index];
            if (dense_pos == std.math.maxInt(u32)) return null;
            if (dense_pos >= self.dense.items.len) return null;
            if (!Entity.eql(self.entities.items[dense_pos], entity)) return null;
            return &self.dense.items[dense_pos];
        }

        /// Remove a component from an entity. Returns true if component was present.
        pub fn remove(self: *Self, allocator: std.mem.Allocator, entity: Entity) bool {
            _ = allocator;
            if (entity.index >= self.sparse.items.len) return false;
            if (self.generations.items[entity.index] != entity.generation) return false;
            const dense_pos = self.sparse.items[entity.index];
            if (dense_pos == std.math.maxInt(u32)) return false;
            if (dense_pos >= self.dense.items.len) return false;
            if (!Entity.eql(self.entities.items[dense_pos], entity)) return false;

            // Swap with last element
            const last_pos = self.dense.items.len - 1;
            if (dense_pos != last_pos) {
                const last_entity = self.entities.items[last_pos];
                self.dense.items[dense_pos] = self.dense.items[last_pos];
                self.entities.items[dense_pos] = self.entities.items[last_pos];
                self.sparse.items[last_entity.index] = @intCast(dense_pos);
            }
            self.dense.items.len -= 1;
            self.entities.items.len -= 1;
            self.sparse.items[entity.index] = std.math.maxInt(u32);
            return true;
        }

        /// Check if an entity has this component.
        pub fn has(self: Self, entity: Entity) bool {
            return self.get(entity) != null;
        }

        /// Iterate over all components with their entities.
        pub fn iterator(self: Self) struct { items: []T, entities: []Entity } {
            return .{
                .items = self.dense.items,
                .entities = self.entities.items,
            };
        }

        /// Number of components stored.
        pub fn count(self: Self) usize {
            return self.dense.items.len;
        }
    };
}

test "component storage add and get" {
    const allocator = std.testing.allocator;
    var storage = try ComponentStorage(f32).init(allocator, 10);
    defer storage.deinit(allocator);

    const entity = Entity.init(0, 1);
    try storage.add(allocator, entity, 42.0);

    const value = storage.get(entity);
    try std.testing.expect(value != null);
    try std.testing.expectEqual(@as(f32, 42.0), value.?.*);
}

test "component storage remove" {
    const allocator = std.testing.allocator;
    var storage = try ComponentStorage(u32).init(allocator, 10);
    defer storage.deinit(allocator);

    const entity = Entity.init(0, 1);
    try storage.add(allocator, entity, 100);

    try std.testing.expect(storage.has(entity));
    try std.testing.expect(storage.remove(allocator, entity));
    try std.testing.expect(!storage.has(entity));
}

test "component storage get non-existent" {
    const allocator = std.testing.allocator;
    var storage = try ComponentStorage(u32).init(allocator, 10);
    defer storage.deinit(allocator);

    const entity = Entity.init(0, 1);
    try std.testing.expect(storage.get(entity) == null);
}

test "component storage out of date generation" {
    const allocator = std.testing.allocator;
    var storage = try ComponentStorage(u32).init(allocator, 10);
    defer storage.deinit(allocator);

    const e1 = Entity.init(0, 1);
    const e2 = Entity.init(0, 2); // Same index, newer generation
    try storage.add(allocator, e1, 100);
    try std.testing.expect(storage.get(e2) == null);
}

test "component iterator" {
    const allocator = std.testing.allocator;
    var storage = try ComponentStorage(u32).init(allocator, 10);
    defer storage.deinit(allocator);

    const e1 = Entity.init(0, 1);
    const e2 = Entity.init(1, 1);
    try storage.add(allocator, e1, 10);
    try storage.add(allocator, e2, 20);

    const iter = storage.iterator();
    try std.testing.expectEqual(@as(usize, 2), iter.items.len);
}

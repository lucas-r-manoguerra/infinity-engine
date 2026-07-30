//! Entity handle type.
//!
//! An Entity is a 64-bit handle: 32-bit index + 32-bit generation.
//! The generation allows safe entity reuse: after destruction, the generation
//! increments so old handles become invalid.

const std = @import("std");

/// Packed entity handle.
/// Low 32 bits: index into the entity array.
/// High 32 bits: generation for safe reuse.
pub const Entity = packed struct(u64) {
    index: u32,
    generation: u32,

    pub const INVALID: Entity = .{ .index = 0xFFFFFFFF, .generation = 0 };

    /// Create a new entity handle.
    pub fn init(index: u32, generation: u32) Entity {
        return .{ .index = index, .generation = generation };
    }

    /// Check if the entity handle is valid (not the null/invalid sentinel).
    pub fn isValid(self: Entity) bool {
        return self.index != 0xFFFFFFFF;
    }

    /// Compare two entity handles for equality.
    pub fn eql(a: Entity, b: Entity) bool {
        return @as(u64, @bitCast(a)) == @as(u64, @bitCast(b));
    }
};

test "entity creation" {
    const e = Entity.init(0, 1);
    try std.testing.expect(e.isValid());
    try std.testing.expectEqual(@as(u32, 0), e.index);
    try std.testing.expectEqual(@as(u32, 1), e.generation);
}

test "entity INVALID sentinel" {
    try std.testing.expect(!Entity.INVALID.isValid());
}

test "entity equality" {
    const a = Entity.init(5, 2);
    const b = Entity.init(5, 2);
    const c = Entity.init(5, 3);
    try std.testing.expect(Entity.eql(a, b));
    try std.testing.expect(!Entity.eql(a, c));
}

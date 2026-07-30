//! Blueprint Virtual Machine skeleton.
//!
//! The Blueprint VM executes compiled node graphs.
//! Nodes are connected by pins and edges, forming a dataflow graph.
//! The VM traverses the graph, executing nodes in topological order.
//!
//! Full implementation: Milestone 7.

const std = @import("std");

/// Unique identifier for a node in the graph.
pub const NodeId = u32;

/// Type of value flowing through a pin.
pub const PinType = enum {
    float,
    int,
    bool,
    string,
    vector3,
    entity,
    event,
    exec, // Execution flow pin
};

/// Direction of a pin.
pub const PinDirection = enum {
    input,
    output,
};

/// A pin on a node (connection point).
pub const Pin = struct {
    id: u32,
    name: []const u8,
    pin_type: PinType,
    direction: PinDirection,
    wire_id: ?u32 = null,
};

/// Execution context for the VM.
pub const VMContext = struct {
    allocator: std.mem.Allocator,
    node_count: u32 = 0,
    ready: bool = false,

    pub fn init(allocator: std.mem.Allocator) VMContext {
        return .{ .allocator = allocator, .ready = true };
    }

    pub fn deinit(self: *VMContext) void {
        _ = self;
    }
};

test "blueprint vm context" {
    const allocator = std.testing.allocator;
    var ctx = VMContext.init(allocator);
    defer ctx.deinit();
    try std.testing.expect(ctx.ready);
}

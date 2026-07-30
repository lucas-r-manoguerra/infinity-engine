//! Blueprint Node Graph.
//!
//! A graph is a collection of nodes connected by wires/edges.
//! The graph is compiled and executed by the Blueprint VM.

const std = @import("std");
const vm = @import("vm.zig");
const node = @import("node.zig");

/// A connection wire between two pins.
pub const Wire = struct {
    id: u32,
    from_node: vm.NodeId,
    from_pin: u32,
    to_node: vm.NodeId,
    to_pin: u32,
};

/// A Blueprint graph.
pub const Graph = struct {
    nodes: std.ArrayListUnmanaged(node.Node),
    wires: std.ArrayListUnmanaged(Wire),
    allocator: std.mem.Allocator,

    pub fn init(allocator: std.mem.Allocator) Graph {
        return .{
            .allocator = allocator,
            .nodes = .{ .items = &.{}, .capacity = 0 },
            .wires = .{ .items = &.{}, .capacity = 0 },
        };
    }

    pub fn deinit(self: *Graph) void {
        self.nodes.deinit(self.allocator);
        self.wires.deinit(self.allocator);
    }

    /// Add a node to the graph.
    pub fn addNode(self: *Graph, n: node.Node) !vm.NodeId {
        const id = @as(vm.NodeId, @intCast(self.nodes.items.len));
        try self.nodes.append(self.allocator, n);
        return id;
    }

    /// Connect two pins with a wire.
    pub fn connect(self: *Graph, from_node: vm.NodeId, from_pin: u32, to_node: vm.NodeId, to_pin: u32) !void {
        try self.wires.append(self.allocator, .{
            .id = @intCast(self.wires.items.len),
            .from_node = from_node,
            .from_pin = from_pin,
            .to_node = to_node,
            .to_pin = to_pin,
        });
    }

    /// Number of nodes in the graph.
    pub fn nodeCount(self: Graph) usize {
        return self.nodes.items.len;
    }

    /// Number of wires in the graph.
    pub fn wireCount(self: Graph) usize {
        return self.wires.items.len;
    }
};

test "graph add nodes and connect" {
    const allocator = std.testing.allocator;
    var g = Graph.init(allocator);
    defer g.deinit();

    const n1 = try g.addNode(.{ .id = 0, .name = "Start", .category = .event });
    const n2 = try g.addNode(.{ .id = 1, .name = "Print", .category = .action });

    try g.connect(n1, 0, n2, 0);

    try std.testing.expectEqual(@as(usize, 2), g.nodeCount());
    try std.testing.expectEqual(@as(usize, 1), g.wireCount());
}

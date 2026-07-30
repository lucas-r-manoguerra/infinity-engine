//! Blueprint Node definitions.
//!
//! Nodes are the fundamental building blocks of Blueprint graphs.
//! Each node has inputs, outputs, and an execution function.

const std = @import("std");
const vm = @import("vm.zig");

/// Category of a blueprint node.
pub const NodeCategory = enum {
    event,
    action,
    condition,
    math,
    variable,
    flow,
    custom,
};

/// Blueprint node definition.
pub const Node = struct {
    id: vm.NodeId,
    name: []const u8,
    category: NodeCategory,
    inputs: []vm.Pin = &.{},
    outputs: []vm.Pin = &.{},
};

test "node definition" {
    const node = Node{
        .id = 0,
        .name = "Print String",
        .category = .action,
    };
    try std.testing.expectEqualStrings("Print String", node.name);
}

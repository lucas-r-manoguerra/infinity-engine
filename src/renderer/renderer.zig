//! Renderer public interface.
//!
//! Defines the `Backend` tagged union that all backends implement.
//! Currently: `SoftwareBackend` and `VulkanBackend` (unstable preview).

const std = @import("std");
const Color = @import("../core/color.zig").Color;
const platform = @import("../platform/window.zig");

pub const RenderConfig = struct {
    width: u32,
    height: u32,
};

pub const Vertex = struct {
    x: f32,
    y: f32,
    z: f32,
    nx: f32,
    ny: f32,
    nz: f32,
    u: f32,
    v: f32,
    inv_w: f32,
    color: Color,
};

const SoftwareBackend = @import("software.zig").SoftwareBackend;
const VulkanBackend = @import("vulkan.zig").VulkanBackend;

pub const Backend = union(enum) {
    software: SoftwareBackend,
    ///@experimental — API may change before Vulkan backend stabilises
    vulkan: VulkanBackend,

    pub fn init(allocator: std.mem.Allocator, window: *platform.Window, width: u32, height: u32, comptime variant: std.meta.Tag(Backend)) !Backend {
        return switch (variant) {
            .software => Backend{ .software = try SoftwareBackend.init(allocator, window, width, height) },
            .vulkan => Backend{ .vulkan = try VulkanBackend.init(allocator, window, width, height) },
        };
    }

    pub fn deinit(self: *Backend) void {
        switch (self.*) {
            inline else => |*b| b.deinit(),
        }
    }

    pub fn beginFrame(self: *Backend, color: Color) void {
        switch (self.*) {
            inline else => |*b| b.beginFrame(color),
        }
    }

    pub fn endFrame(self: *Backend) void {
        switch (self.*) {
            inline else => |*b| b.endFrame(),
        }
    }

    pub fn drawTriangle(self: *Backend, v0: Vertex, v1: Vertex, v2: Vertex, texture: []const u8) void {
        switch (self.*) {
            inline else => |*b| b.drawTriangle(v0, v1, v2, texture),
        }
    }

    pub fn present(self: *Backend) void {
        switch (self.*) {
            inline else => |*b| b.present(),
        }
    }
};

test "Backend has .vulkan variant" {
    const has_vulkan = comptime blk: {
        const fields = @typeInfo(Backend).@"union".fields;
        for (fields) |f| {
            if (std.mem.eql(u8, f.name, "vulkan")) break :blk true;
        }
        break :blk false;
    };
    try testing.expect(has_vulkan);
}

test "Backend present takes no extra arguments" {
    const sig = @typeInfo(@TypeOf(Backend.present)).@"fn";
    try testing.expectEqual(sig.params.len, 1);
}

test "Backend getFramebuffer is removed" {
    const has_getFb = comptime blk: {
        const decls = @typeInfo(Backend).@"union".decls;
        for (decls) |decl| {
            if (std.mem.eql(u8, decl.name, "getFramebuffer")) {
                break :blk true;
            }
        }
        break :blk false;
    };
    try testing.expect(!has_getFb);
}

const testing = std.testing;

const std = @import("std");
const testing = std.testing;
const Vertex = @import("../../renderer/renderer.zig").Vertex;
const generateSphere = @import("../../renderer/software.zig").generateSphere;
const generateTorus = @import("../../renderer/software.zig").generateTorus;

// 16×16 sphere rings×segments → 2*16*16 = 512 triangles → 512*3 = 1536 vertices
test "sphere 16x16 produces 512 triangles" {
    const verts = try generateSphere(testing.allocator, 16, 16, 1.0);
    defer testing.allocator.free(verts);
    try testing.expectEqual(@as(usize, 1536), verts.len);
}

// 16×16 torus segments → 2*16*16 = 512 triangles → 512*3 = 1536 vertices
test "torus 16 segments produces 512 triangles" {
    const verts = try generateTorus(testing.allocator, 2.0, 1.0, 16);
    defer testing.allocator.free(verts);
    try testing.expectEqual(@as(usize, 1536), verts.len);
}

// All sphere normals must be within unit-length tolerance [0.99, 1.01]
test "sphere normals are unit length" {
    const verts = try generateSphere(testing.allocator, 16, 16, 1.0);
    defer testing.allocator.free(verts);
    for (verts) |v| {
        const len_sq = v.nx * v.nx + v.ny * v.ny + v.nz * v.nz;
        try testing.expect(len_sq >= 0.9801 and len_sq <= 1.0201);
    }
}

// All torus normals must be within unit-length tolerance [0.99, 1.01]
test "torus normals are unit length" {
    const verts = try generateTorus(testing.allocator, 2.0, 1.0, 16);
    defer testing.allocator.free(verts);
    for (verts) |v| {
        const len_sq = v.nx * v.nx + v.ny * v.ny + v.nz * v.nz;
        try testing.expect(len_sq >= 0.9801 and len_sq <= 1.0201);
    }
}

// Guards: rings < 3 returns empty slice
test "sphere guards rings < 3" {
    const verts = try generateSphere(testing.allocator, 2, 16, 1.0);
    defer testing.allocator.free(verts);
    try testing.expectEqual(@as(usize, 0), verts.len);
}

// Guards: segments < 3 returns empty slice
test "sphere guards segments < 3" {
    const verts = try generateSphere(testing.allocator, 16, 2, 1.0);
    defer testing.allocator.free(verts);
    try testing.expectEqual(@as(usize, 0), verts.len);
}

// Guards: segments < 3 returns empty slice
test "torus guards segments < 3" {
    const verts = try generateTorus(testing.allocator, 2.0, 1.0, 2);
    defer testing.allocator.free(verts);
    try testing.expectEqual(@as(usize, 0), verts.len);
}

// Guards: major_r <= minor_r returns empty slice
test "torus guards major_r <= minor_r" {
    const verts = try generateTorus(testing.allocator, 1.0, 2.0, 16);
    defer testing.allocator.free(verts);
    try testing.expectEqual(@as(usize, 0), verts.len);
}

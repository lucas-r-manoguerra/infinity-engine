const std = @import("std");
const testing = std.testing;
const Vertex = @import("../../renderer/renderer.zig").Vertex;

test "Vertex size with normals is 40 bytes" {
    // Vertex = x,y,z,nx,ny,nz,u,v,inv_w (9 x f32 @ 4 = 36) + Color (packed u32, 4) = 40 bytes
    try testing.expectEqual(@as(usize, 40), @sizeOf(Vertex));
}

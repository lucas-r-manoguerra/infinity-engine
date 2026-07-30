const std = @import("std");
const testing = std.testing;
const generateBrickTexture = @import("../../renderer/software.zig").generateBrickTexture;
const TEXTURE_W = @import("../../renderer/software.zig").TEXTURE_W;
const TEXTURE_H = @import("../../renderer/software.zig").TEXTURE_H;

// Texture must be exactly 64×64 BGRA32 = 16384 bytes
test "brick texture size is 64x64x4" {
    const tex = generateBrickTexture();
    try testing.expectEqual(@as(usize, 64 * 64 * 4), tex.len);
}

// Same column (x=10), different brick rows, should have different colors due to stagger.
test "brick stagger produces different colors in same column" {
    const tex = generateBrickTexture();
    // Pixel at (x=10, y=10): brick_row=1 (odd, offset 8), cell_x=(10+8)%16=2, cell_y=10%8=2 → inside brick
    const px_a = (10 * TEXTURE_W + 10) * 4;
    // Pixel at (x=10, y=18): brick_row=2 (even), cell_x=10%16=10, cell_y=18%8=2 → inside brick
    const px_b = (18 * TEXTURE_W + 10) * 4;
    // Different brick rows → hash produces different red channel values
    try testing.expect(tex[px_a + 2] != tex[px_b + 2]);
}

// Mortar pixel must be darker (lower R value) than an adjacent brick pixel.
test "brick mortar is darker than brick face" {
    const tex = generateBrickTexture();
    // Mortar pixel at (x=1, y=1) — inside the 2px mortar border at top-left corner
    const mortar_idx = (1 * TEXTURE_W + 1) * 4;
    // Brick pixel at (x=5, y=5) — inside brick (cell_x=5, cell_y=5, both >= 2)
    const brick_idx = (5 * TEXTURE_W + 5) * 4;

    const mortar_r = tex[mortar_idx + 2];
    const brick_r = tex[brick_idx + 2];
    try testing.expect(brick_r > mortar_r);
}

//! Core color types.
const std = @import("std");

pub const Color = packed struct(u32) {
    b: u8,
    g: u8,
    r: u8,
    a: u8,
};

pub const COLORS = struct {
    pub const dark = Color{ .b = 10, .g = 8, .r = 12, .a = 255 };
    pub const cyan = Color{ .b = 255, .g = 204, .r = 0, .a = 255 };
    pub const magenta = Color{ .b = 204, .g = 0, .r = 255, .a = 255 };
    pub const yellow = Color{ .b = 0, .g = 255, .r = 255, .a = 255 };
    pub const red = Color{ .b = 30, .g = 30, .r = 255, .a = 255 };
    pub const green = Color{ .b = 30, .g = 255, .r = 30, .a = 255 };
    pub const blue = Color{ .b = 255, .g = 30, .r = 30, .a = 255 };
    pub const white = Color{ .b = 255, .g = 255, .r = 255, .a = 255 };
    pub const gray = Color{ .b = 60, .g = 60, .r = 60, .a = 255 };
};

pub fn modulateColor(a: Color, b: Color) Color {
    return Color{
        .b = @as(u8, @intCast((@as(u32, a.b) * @as(u32, b.b)) / 255)),
        .g = @as(u8, @intCast((@as(u32, a.g) * @as(u32, b.g)) / 255)),
        .r = @as(u8, @intCast((@as(u32, a.r) * @as(u32, b.r)) / 255)),
        .a = 255,
    };
}

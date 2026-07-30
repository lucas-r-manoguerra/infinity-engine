//! Pre-compiled SPIR-V vertex shader for the Vulkan backend MVP.
//!
//! SPIR-V binary for a vertex shader that outputs a hardcoded position
//! at the origin: gl_Position = vec4(0.0, 0.0, 0.5, 1.0)
//!
//! Equivalent GLSL:
//! ```glsl
//! #version 450
//! void main() {
//!     gl_Position = vec4(0.0, 0.0, 0.5, 1.0);
//! }
//! ```
//!
//! To update from GLSL source:
//!   glslangValidator -V triangle.vert -o triangle_vert.spv
//!   xxd -i triangle_vert.spv | head -N

const std = @import("std");
const testing = std.testing;

/// Vertex shader SPIR-V binary: writes gl_Position = vec4(0.0, 0.0, 0.5, 1.0)
///
/// SPIR-V v1.0 module with BuiltIn Position output.
pub const VERTEX_SPV = [_]u32{
    // ===== Header (5 words) =====
    0x07230203, // Magic number
    0x00010000, // Version 1.0
    0x00000000, // Generator (unknown)
    0x0000000D, // Bound = 13 (max ID + 1)
    0x00000000, // Schema (reserved)

    // ===== Instructions (79 words) =====

    // OpCapability Shader
    0x00020011, 0x00000001,

    // OpMemoryModel Logical Simple
    0x0003000E, 0x00000000, 0x00000000,

    // OpEntryPoint Vertex %11 "main" %6 (gl_Position)
    0x0006000F, 0x00000000, 0x0000000B, 0x6E69616D, 0x00000000, 0x00000006,

    // OpName %11 "main"
    0x00040005, 0x0000000B, 0x6E69616D, 0x00000000,

    // OpName %6 "gl_Position"
    0x00050005, 0x00000006, 0x505F6C67, 0x6974736F, 0x006F6E69,

    // OpDecorate %6 BuiltIn Position(0)
    0x0004000C, 0x00000006, 0x0000000B, 0x00000000,

    // %1 = OpTypeVoid
    0x00020013, 0x00000001,

    // %2 = OpTypeFunction %1
    0x00030021, 0x00000002, 0x00000001,

    // %3 = OpTypeFloat 32
    0x00030016, 0x00000003, 0x00000020,

    // %4 = OpTypeVector %3 4
    0x00040017, 0x00000004, 0x00000003, 0x00000004,

    // %5 = OpTypePointer Output(1) %4
    0x00040020, 0x00000005, 0x00000001, 0x00000004,

    // %6 = OpVariable %5 Output(1)  —  gl_Position
    0x00040036, 0x00000006, 0x00000005, 0x00000001,

    // %7 = OpConstant %3 0.0
    0x0004002B, 0x00000007, 0x00000003, 0x00000000,

    // %8 = OpConstant %3 0.5
    0x0004002B, 0x00000008, 0x00000003, 0x3F000000,

    // %9 = OpConstant %3 1.0
    0x0004002B, 0x00000009, 0x00000003, 0x3F800000,

    // %10 = OpConstantComposite %4 %7 %7 %8 %9  — vec4(0,0,0.5,1)
    0x0007002C, 0x0000000A, 0x00000004, 0x00000007, 0x00000007, 0x00000008, 0x00000009,

    // %11 = OpFunction %1 None(0) %2  —  main()
    0x00050026, 0x0000000B, 0x00000001, 0x00000000, 0x00000002,

    // %12 = OpLabel
    0x00020027, 0x0000000C,

    // OpStore %6 %10  — gl_Position = vec4(0,0,0.5,1)
    0x0003003B, 0x00000006, 0x0000000A,

    // OpReturn
    0x000100FD,

    // OpFunctionEnd
    0x00010038,
};

test "vertex SPIR-V has correct magic number" {
    try testing.expectEqual(VERTEX_SPV[0], 0x07230203);
}

test "vertex SPIR-V header version is 1.0" {
    try testing.expectEqual(VERTEX_SPV[1], 0x00010000);
}

test "vertex SPIR-V has non-zero bound" {
    try testing.expect(VERTEX_SPV[3] > 0);
}

test "vertex SPIR-V total word count is correct" {
    try testing.expectEqual(VERTEX_SPV.len, 80);
}

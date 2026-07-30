//! Pre-compiled SPIR-V fragment shader for the Vulkan backend MVP.
//!
//! SPIR-V binary for a fragment shader that outputs solid red:
//! outColor = vec4(1.0, 0.0, 0.0, 1.0)
//!
//! Equivalent GLSL:
//! ```glsl
//! #version 450
//! layout(location = 0) out vec4 outColor;
//! void main() {
//!     outColor = vec4(1.0, 0.0, 0.0, 1.0);
//! }
//! ```
//!
//! To update from GLSL source:
//!   glslangValidator -V triangle.frag -o triangle_frag.spv
//!   xxd -i triangle_frag.spv | head -N

const std = @import("std");
const testing = std.testing;

/// Fragment shader SPIR-V binary: outputs vec4(1.0, 0.0, 0.0, 1.0) at location 0
///
/// SPIR-V v1.0 module with a single color output.
pub const FRAGMENT_SPV = [_]u32{
    // ===== Header (5 words) =====
    0x07230203, // Magic number
    0x00010000, // Version 1.0
    0x00000000, // Generator (unknown)
    0x0000000C, // Bound = 12 (max ID + 1)
    0x00000000, // Schema (reserved)

    // ===== Instructions (78 words) =====

    // OpCapability Shader
    0x00020011, 0x00000001,

    // OpMemoryModel Logical Simple
    0x0003000E, 0x00000000, 0x00000000,

    // OpEntryPoint Fragment %10 "main" %6 (outColor)
    0x0006000F, 0x00000004, 0x0000000A, 0x6E69616D, 0x00000000, 0x00000006,

    // OpExecutionMode %10 OriginUpperLeft(0)
    0x00030010, 0x0000000A, 0x00000000,

    // OpName %10 "main"
    0x00040005, 0x0000000A, 0x6E69616D, 0x00000000,

    // OpName %6 "outColor"
    0x00050005, 0x00000006, 0x4374756F, 0x726F6C6F, 0x00000000,

    // OpDecorate %6 Location 0
    0x0004000C, 0x00000006, 0x0000001E, 0x00000000,

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

    // %6 = OpVariable %5 Output(1)  —  outColor
    0x00040036, 0x00000006, 0x00000005, 0x00000001,

    // %7 = OpConstant %3 0.0
    0x0004002B, 0x00000007, 0x00000003, 0x00000000,

    // %8 = OpConstant %3 1.0
    0x0004002B, 0x00000008, 0x00000003, 0x3F800000,

    // %9 = OpConstantComposite %4 %8 %7 %7 %8  — vec4(1,0,0,1)
    0x0007002C, 0x00000009, 0x00000004, 0x00000008, 0x00000007, 0x00000007, 0x00000008,

    // %10 = OpFunction %1 None(0) %2  —  main()
    0x00050026, 0x0000000A, 0x00000001, 0x00000000, 0x00000002,

    // %11 = OpLabel
    0x00020027, 0x0000000B,

    // OpStore %6 %9  — outColor = vec4(1,0,0,1)
    0x0003003B, 0x00000006, 0x00000009,

    // OpReturn
    0x000100FD,

    // OpFunctionEnd
    0x00010038,
};

test "fragment SPIR-V has correct magic number" {
    try testing.expectEqual(FRAGMENT_SPV[0], 0x07230203);
}

test "fragment SPIR-V header version is 1.0" {
    try testing.expectEqual(FRAGMENT_SPV[1], 0x00010000);
}

test "fragment SPIR-V has non-zero bound" {
    try testing.expect(FRAGMENT_SPV[3] > 0);
}

test "fragment SPIR-V total word count is correct" {
    try testing.expectEqual(FRAGMENT_SPV.len, 79);
}

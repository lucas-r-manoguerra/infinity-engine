const std = @import("std");
const testing = std.testing;
const vk = @import("../src/renderer/vk.zig");

test "VkPipelineShaderStageCreateInfo size" {
    try testing.expectEqual(@sizeOf(vk.VkPipelineShaderStageCreateInfo), 48);
}

test "VkShaderModule size" {
    try testing.expectEqual(@sizeOf(vk.VkShaderModule), 8);
}

test "VkPipeline size" {
    try testing.expectEqual(@sizeOf(vk.VkPipeline), 8);
}

test "?VkPipeline size" {
    try testing.expectEqual(@sizeOf(?vk.VkPipeline), 8);
}

test "VkPipelineLayout size" {
    try testing.expectEqual(@sizeOf(vk.VkPipelineLayout), 8);
}

test "VkRenderPass size" {
    try testing.expectEqual(@sizeOf(vk.VkRenderPass), 8);
}

test "VkGraphicsPipelineCreateInfo size" {
    try testing.expectEqual(@sizeOf(vk.VkGraphicsPipelineCreateInfo), 144);
}

test "VkPipelineVertexInputStateCreateInfo size" {
    try testing.expectEqual(@sizeOf(vk.VkPipelineVertexInputStateCreateInfo), 40);
}

test "VkPipelineInputAssemblyStateCreateInfo size" {
    try testing.expectEqual(@sizeOf(vk.VkPipelineInputAssemblyStateCreateInfo), 24);
}

test "VkPipelineViewportStateCreateInfo size" {
    try testing.expectEqual(@sizeOf(vk.VkPipelineViewportStateCreateInfo), 40);
}

test "VkPipelineRasterizationStateCreateInfo size" {
    try testing.expectEqual(@sizeOf(vk.VkPipelineRasterizationStateCreateInfo), 56);
}

test "VkPipelineMultisampleStateCreateInfo size" {
    try testing.expectEqual(@sizeOf(vk.VkPipelineMultisampleStateCreateInfo), 40);
}

test "VkPipelineColorBlendStateCreateInfo size" {
    try testing.expectEqual(@sizeOf(vk.VkPipelineColorBlendStateCreateInfo), 40);
}

test "VkPipelineColorBlendAttachmentState size" {
    try testing.expectEqual(@sizeOf(vk.VkPipelineColorBlendAttachmentState), 32);
}

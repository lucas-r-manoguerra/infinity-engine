//! AI Integration — Core types and configuration.
//!
//! This module defines the AI integration layer skeleton.
//! From day 1, the engine has a place for AI to live.
//! Full implementation comes in Milestone 6.

const std = @import("std");

/// AI provider/backend type.
pub const AIProvider = enum {
    none,
    // Future: openai, anthropic, local, etc.
};

/// AI system configuration.
pub const AIConfig = struct {
    provider: AIProvider = .none,
    model: []const u8 = "",
    api_endpoint: []const u8 = "",
    max_context_tokens: u32 = 4096,
    enabled: bool = false,
};

/// Snapshot of engine state for AI context.
/// Serialized and sent to the AI when context is needed.
pub const ContextSnapshot = struct {
    frame_count: u64 = 0,
    entity_count: u32 = 0,
    fps: u32 = 0,
    scene_description: []const u8 = "",
};

/// AI agent handle.
pub const AgentHandle = u32;

/// Result from an AI operation.
pub const AIResult = struct {
    success: bool,
    output: []const u8,
    error_message: []const u8 = "",
};

/// The AI integration context.
pub const AIContext = struct {
    allocator: std.mem.Allocator,
    config: AIConfig,
    initialized: bool = false,

    pub fn init(allocator: std.mem.Allocator, config: AIConfig) AIContext {
        return .{
            .allocator = allocator,
            .config = config,
            .initialized = true,
        };
    }

    pub fn deinit(self: *AIContext) void {
        _ = self;
    }

    /// Check if AI integration is available.
    pub fn isAvailable(self: AIContext) bool {
        return self.initialized and self.config.enabled;
    }
};

test "ai context creation" {
    const allocator = std.testing.allocator;
    var ctx = AIContext.init(allocator, .{});
    defer ctx.deinit();
    try std.testing.expect(ctx.initialized);
    try std.testing.expect(!ctx.isAvailable());
}

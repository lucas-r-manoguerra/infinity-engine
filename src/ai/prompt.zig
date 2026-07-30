//! Prompt templates for AI interactions.
//!
//! Structured prompts for common AI operations in the engine.
//! Each template defines the input parameters and output format.

const std = @import("std");

/// A compiled prompt template ready for submission.
pub const Prompt = struct {
    system: []const u8 = "",
    user: []const u8 = "",
    temperature: f32 = 0.7,
};

/// Prompt templates for common operations.
pub const Templates = struct {
    /// Generate a Zig component from a description.
    pub fn codeGeneration(description: []const u8) Prompt {
        return .{
            .system = "You are an expert Zig game engine programmer. Generate production-quality, safe Zig code.",
            .user = description,
            .temperature = 0.3,
        };
    }

    /// Generate a Blueprint graph from a gameplay description.
    pub fn blueprintGeneration(description: []const u8) Prompt {
        return .{
            .system = "You are a game designer expert in visual scripting. Generate Blueprint node graphs.",
            .user = description,
            .temperature = 0.5,
        };
    }

    /// Debug an error with full engine context.
    pub fn debugContext(error_msg: []const u8, context: []const u8) Prompt {
        _ = context;
        return .{
            .system = "You are a debugging assistant for the Infinity Engine. Analyze the error with full context.",
            .user = error_msg,
            .temperature = 0.2,
        };
    }
};

test "prompt template creation" {
    const prompt = Templates.codeGeneration("Create a player movement component with WASD input.");
    try std.testing.expect(prompt.system.len > 0);
    try std.testing.expect(prompt.user.len > 0);
}

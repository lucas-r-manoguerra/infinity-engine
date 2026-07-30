//! AI Agent abstraction.
//!
//! Agents process natural language requests and produce engine commands,
//! code, or Blueprint graphs. Each agent specializes in a domain.

const std = @import("std");
const core = @import("core.zig");

/// Agent specialization domain.
pub const AgentDomain = enum {
    /// General purpose assistant
    general,
    /// Code generation (Zig)
    code_generation,
    /// Blueprint graph generation
    blueprint_generation,
    /// Game design assistance
    game_design,
    /// Debugging and analysis
    debugging,
};

/// Agent configuration.
pub const AgentConfig = struct {
    domain: AgentDomain = .general,
    name: []const u8 = "",
    system_prompt: []const u8 = "",
    temperature: f32 = 0.7,
    max_tokens: u32 = 2048,
};

/// Agent instance.
pub const Agent = struct {
    config: AgentConfig,
    context: *core.AIContext,

    pub fn init(context: *core.AIContext, config: AgentConfig) Agent {
        return .{ .context = context, .config = config };
    }
};

test "agent creation" {
    const allocator = std.testing.allocator;
    var ctx = core.AIContext.init(allocator, .{});
    defer ctx.deinit();

    const agent = Agent.init(&ctx, .{
        .domain = .code_generation,
        .name = "CodeSmith",
    });
    try std.testing.expectEqualStrings("CodeSmith", agent.config.name);
}

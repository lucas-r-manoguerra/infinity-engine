//! Infinity Engine — Public API.
//!
//! This is the root module for the Infinity Engine library.
//! Re-exports all public types from every subsystem.

// Core
pub const engine = @import("core/engine.zig");
pub const Engine = engine.Engine;
pub const Config = engine.Config;
pub const GameLoop = @import("core/loop.zig").GameLoop;
pub const LoopCallbacks = @import("core/loop.zig").LoopCallbacks;
pub const FIXED_DT = @import("core/loop.zig").FIXED_DT;
pub const Clock = @import("core/time.zig").Clock;
pub const DeltaTime = @import("core/time.zig").DeltaTime;
pub const FpsCounter = @import("core/time.zig").FpsCounter;
pub const Timer = @import("core/time.zig").Timer;
pub const Arena = @import("core/memory.zig").Arena;
pub const FrameAllocator = @import("core/memory.zig").FrameAllocator;
pub const Error = @import("core/error.zig").Error;

// Math
pub const Vec3 = @import("math/vec3.zig").Vec3;
pub const Vec4 = @import("math/vec4.zig").Vec4;
pub const Mat4 = @import("math/mat4.zig").Mat4;
pub const Quat = @import("math/quat.zig").Quat;
pub const Transform = @import("math/transform.zig").Transform;

// ECS
pub const Entity = @import("ecs/entity.zig").Entity;
pub const World = @import("ecs/world.zig").World;
pub const ComponentRegistry = @import("ecs/component.zig").ComponentRegistry;
pub const System = @import("ecs/system.zig").System;
pub const SystemPhase = @import("ecs/system.zig").SystemPhase;
pub const SystemGraph = @import("ecs/system.zig").SystemGraph;

// Platform
pub const Window = @import("platform/window.zig").Window;
pub const InputState = @import("platform/input.zig").InputState;

// Renderer
pub const Renderer = @import("renderer/renderer.zig").Renderer;
pub const Color = @import("renderer/renderer.zig").Color;

// AI
pub const AIContext = @import("ai/core.zig").AIContext;
pub const AIConfig = @import("ai/core.zig").AIConfig;
pub const Agent = @import("ai/agent.zig").Agent;

// Blueprint
pub const BlueprintVM = @import("blueprint/vm.zig").VMContext;
pub const BlueprintGraph = @import("blueprint/graph.zig").Graph;
pub const BlueprintNode = @import("blueprint/node.zig").Node;

/// Engine version.
pub const VERSION: []const u8 = "0.1.0-mvp";

//! Test runner — imports every module with tests so they all share
//! `src/` as the module root and cross-directory `@import` works.
//!
//! Run via: `zig build test`

const std = @import("std");

// Core
// NOTE: engine.zig tests require X11 display — not imported here
test { _ = @import("core/time.zig"); }
test { _ = @import("core/memory.zig"); }
test { _ = @import("core/loop.zig"); }

// Math
test { _ = @import("math/vec3.zig"); }
test { _ = @import("math/mat4.zig"); }
test { _ = @import("math/quat.zig"); }
test { _ = @import("math/transform.zig"); }

// ECS
test { _ = @import("ecs/entity.zig"); }
test { _ = @import("ecs/component.zig"); }
test { _ = @import("ecs/world.zig"); }
test { _ = @import("ecs/system.zig"); }
test { _ = @import("ecs/query.zig"); }

// Platform
test { _ = @import("platform/window.zig"); }
test { _ = @import("platform/input.zig"); }

// Renderer
test { _ = @import("renderer/renderer.zig"); }
test { _ = @import("renderer/software.zig"); }
test { _ = @import("tests/renderer/vertex_test.zig"); }
test { _ = @import("tests/renderer/mesh_generation_test.zig"); }
test { _ = @import("tests/renderer/brick_texture_test.zig"); }
test { _ = @import("tests/runtime/engine_scene_test.zig"); }
test { _ = @import("renderer/vk.zig"); }
test { _ = @import("renderer/vk_loader.zig"); }
test { _ = @import("renderer/vulkan_xlib.zig"); }
test { _ = @import("renderer/vulkan.zig"); }
test { _ = @import("shaders/triangle_vert.zig"); }
test { _ = @import("shaders/triangle_frag.zig"); }

// AI
test { _ = @import("ai/prompt.zig"); }

// Blueprint
test { _ = @import("blueprint/graph.zig"); }
test { _ = @import("blueprint/node.zig"); }

// Runtime
test { _ = @import("runtime/engine.zig"); }


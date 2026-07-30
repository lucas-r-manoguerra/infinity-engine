//! Infinity Engine — Entry Point.
//!
//! Thin entry point: creates the Engine, runs it, and cleans up.

const std = @import("std");
const engine = @import("runtime/engine.zig");

pub fn main() void {
    var eng = engine.Engine.init(std.heap.page_allocator, .{
        .width = 800,
        .height = 600,
        .title = "Infinity Engine",
    }) catch {
        std.debug.print("[FAIL] engine init failed\n", .{});
        return;
    };

    defer eng.deinit();

    eng.postInit() catch {
        std.debug.print("[FAIL] engine postInit failed\n", .{});
        return;
    };

    eng.run();
}

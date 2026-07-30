//! Infinity Engine — Entry Point.
//!
//! Thin entry point: creates the Engine, runs it, and cleans up.

const std = @import("std");
const engine = @import("runtime/engine.zig");

pub fn main(init: std.process.Init.Minimal) void {
    // Parse CLI args (first arg is executable name, skip it).
    var use_tiles = true;
    for (init.args.vector[1..]) |arg| {
        if (std.mem.eql(u8, std.mem.span(arg), "--no-tile")) {
            use_tiles = false;
        }
    }

    var eng = engine.Engine.init(std.heap.page_allocator, .{
        .width = 800,
        .height = 600,
        .title = "Infinity Engine",
    }, use_tiles) catch {
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

//! Input state management with action mapping.
//!
//! Game code queries logical actions (action_exit, action_jump),
//! never raw key codes or platform-specific input.
//! Platform backend (window.zig) feeds X11 events into setKey.

const std = @import("std");
const x11 = @import("x11.zig");
const default_bindings = @import("../config/default_bindings.zig");

/// Re-export Action so callers can write `input.Action` or `input.isPressed(.action_exit)`.
pub const Action = default_bindings.Action;

/// Input state for a single frame.
pub const InputState = struct {
    keys: [256]bool = [_]bool{false} ** 256,
    keys_previous: [256]bool = [_]bool{false} ** 256,
    mouse_x: f32 = 0,
    mouse_y: f32 = 0,
    mouse_buttons: [3]bool = [_]bool{false} ** 3,
    close_requested: bool = false,

    pub fn init() InputState {
        return .{};
    }

    /// Call at the start of each frame to prepare state.
    pub fn beginFrame(self: *InputState) void {
        self.keys_previous = self.keys;
    }

    /// Set a key state (called by platform backend).
    /// keysym is an X11 KeySym value.
    pub fn setKey(self: *InputState, keysym: x11.KeySym, pressed: bool) void {
        if (keysym < 256) {
            self.keys[@as(u8, @intCast(keysym))] = pressed;
        }
        if (keysym == x11.Key_Escape) self.keys[27] = pressed;
        if (keysym == x11.Key_Space) self.keys[32] = pressed;
        if (keysym == x11.Key_Return) self.keys[13] = pressed;
        if (keysym == 0xFFE1) self.keys[29] = pressed;
        if (keysym == 0xFF09) self.keys[9] = pressed;
        if (keysym == 0x0057) self.keys[119] = pressed;
        if (keysym == 0x0053) self.keys[115] = pressed;
        if (keysym == 0x0041) self.keys[97] = pressed;
        if (keysym == 0x0044) self.keys[100] = pressed;
    }

    /// Is a key currently held down?
    pub fn isKeyDown(self: InputState, key: u8) bool {
        return self.keys[key];
    }

    /// Was a key just pressed this frame?
    pub fn isKeyJustPressed(self: InputState, key: u8) bool {
        return self.keys[key] and !self.keys_previous[key];
    }

    /// Was a key just released this frame?
    pub fn isKeyJustReleased(self: InputState, key: u8) bool {
        return !self.keys[key] and self.keys_previous[key];
    }

    // --- Action queries ---

    fn keyIndexForAction(action: Action) u8 {
        const syms = default_bindings.bindingsForAction(action);
        const keysym = syms[0];
        if (keysym < 256) return @truncate(keysym);
        if (keysym == 0xFF1B) return 27;
        if (keysym == 0x0020) return 32;
        if (keysym == 0xFF0D) return 13;
        if (keysym == 0xFFE1) return 29;
        if (keysym == 0xFF09) return 9;
        if (keysym == 0x0057) return 119;
        if (keysym == 0x0053) return 115;
        if (keysym == 0x0041) return 97;
        if (keysym == 0x0044) return 100;
        return 0;
    }

    /// Is a logical action currently active (held down)?
    pub fn isHeld(self: InputState, action: Action) bool {
        return self.isKeyDown(keyIndexForAction(action));
    }

    /// Was a logical action just pressed this frame?
    pub fn isPressed(self: InputState, action: Action) bool {
        return self.isKeyJustPressed(keyIndexForAction(action));
    }

    /// Was a logical action just released this frame?
    pub fn isReleased(self: InputState, action: Action) bool {
        return self.isKeyJustReleased(keyIndexForAction(action));
    }
};

test "input state key tracking" {
    var state = InputState.init();
    state.beginFrame();

    try std.testing.expect(!state.isKeyDown(32));
    state.setKey(32, true);
    try std.testing.expect(state.isKeyDown(32));
    try std.testing.expect(state.isKeyJustPressed(32));

    state.beginFrame();
    try std.testing.expect(state.isKeyDown(32));
    try std.testing.expect(!state.isKeyJustPressed(32));

    state.setKey(32, false);
    try std.testing.expect(!state.isKeyDown(32));
    try std.testing.expect(state.isKeyJustReleased(32));
}

test "input state escape maps to 27" {
    var state = InputState.init();
    state.setKey(0xFF1B, true);
    try std.testing.expect(state.isKeyDown(27));
}

test "input state action queries" {
    var state = InputState.init();
    state.beginFrame();

    state.setKey(0xFF1B, true);
    try std.testing.expect(state.isPressed(.action_exit));
    try std.testing.expect(!state.isReleased(.action_exit));

    state.beginFrame();
    try std.testing.expect(state.isHeld(.action_exit));
    try std.testing.expect(!state.isPressed(.action_exit));

    state.setKey(0xFF1B, false);
    try std.testing.expect(!state.isHeld(.action_exit));
    try std.testing.expect(state.isReleased(.action_exit));
}

test "input state all action types work" {
    const actions = comptime std.meta.tags(Action);
    var state = InputState.init();
    state.beginFrame();

    inline for (actions) |action| {
        try std.testing.expect(!state.isPressed(action));
        try std.testing.expect(!state.isHeld(action));
    }
}

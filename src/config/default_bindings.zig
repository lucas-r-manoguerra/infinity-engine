//! Default key bindings — Action → X11 KeySym mapping.
//!
//! Actions are logical game inputs. Each action maps to one or more
//! physical keys via their X11 KeySym values.
//! This is the single source of truth for action definitions.

const std = @import("std");
const x11 = @import("../platform/x11.zig");

pub const Action = enum {
    action_exit,
    action_jump,
    action_move_up,
    action_move_down,
    action_move_left,
    action_move_right,
    action_boost,
    action_attack,
    action_interact,

    pub fn count() usize {
        return @typeInfo(Action).@"enum".fields.len;
    }
};

const data: [Action.count()][]const x11.KeySym = .{
    &.{x11.Key_Escape},                       // action_exit
    &.{x11.Key_Space},                        // action_jump
    &.{0x0057},                               // action_move_up   — W
    &.{0x0053},                               // action_move_down — S
    &.{0x0041},                               // action_move_left — A
    &.{0x0044},                               // action_move_right — D
    &.{x11.Key_Shift_L},                      // action_boost
    &.{x11.Key_Tab},                          // action_attack    (placeholder)
    &.{x11.Key_Return},                       // action_interact
};

/// Returns the KeySym slice for a given action.
pub fn bindingsForAction(action: Action) []const x11.KeySym {
    return data[@intFromEnum(action)];
}

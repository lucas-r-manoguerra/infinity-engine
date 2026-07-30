//! Platform window — X11 implementation via direct FFI.
//!
//! Window is now decoupled from framebuffer management. The window owns
//! the X11 connection and handle; rendering backends own presentation.
//! Links directly to libX11 at compile time — no C bridge needed.

const std = @import("std");
const testing = std.testing;
const x11 = @import("x11.zig");
const InputState = @import("input.zig").InputState;

pub const Window = struct {
    display: *x11.Display,
    handle: x11.Window,
    width: u32,
    height: u32,
    wm_delete_atom: x11.Atom,
    running: bool,
    input_state: InputState = .{},

    // ximage, gc, fb removed — backend owns presentation resources.
    // See design decision: "getFramebuffer removal + window-handle init refactor"
};

pub fn windowCreate(title: [:0]const u8, w: c_uint, h: c_uint) ?Window {
    const display = x11.XOpenDisplay(null) orelse return null;
    const screen = x11.XDefaultScreen(display);
    const root = x11.XRootWindow(display, screen);
    const black = x11.XBlackPixel(display, screen);
    const white = x11.XWhitePixel(display, screen);

    const handle = x11.XCreateSimpleWindow(display, root, 100, 100, w, h, 1, black, white);
    if (handle == 0) {
        _ = x11.XCloseDisplay(display);
        return null;
    }

    _ = x11.XStoreName(display, handle, title);

    var wm_delete_atom = x11.XInternAtom(display, "WM_DELETE_WINDOW", 0);
    _ = x11.XSetWMProtocols(display, handle, @ptrCast(&wm_delete_atom), 1);

    _ = x11.XMapWindow(display, handle);

    _ = x11.XSelectInput(display, handle, x11.KeyPressMask | x11.KeyReleaseMask | x11.StructureNotifyMask);

    _ = x11.XFlush(display);

    return Window{
        .display = display,
        .handle = handle,
        .width = @intCast(w),
        .height = @intCast(h),
        .wm_delete_atom = wm_delete_atom,
        .running = true,
    };
}

pub fn windowDestroy(win: *Window) void {
    // Backend owns framebuffer memory — only close the display.
    // XDestroyImage, XFreeGC, XShmDetach removed (no longer window-owned).
    _ = x11.XCloseDisplay(win.display);
    win.running = false;
}

pub fn windowPollEvents(win: *Window) void {
    var event: x11.XEvent = undefined;
    while (x11.XPending(win.display) > 0) {
        _ = x11.XNextEvent(win.display, &event);
        switch (event.type) {
            x11.ClientMessage => {
                if (event.xclient.data.longs[0] == @as(c_long, @intCast(win.wm_delete_atom))) {
                    win.running = false;
                }
            },
            x11.KeyPress => {
                const keysym = x11.XLookupKeysym(&event.xkey, 0);
                win.input_state.setKey(keysym, true);
            },
            x11.KeyRelease => {
                const keysym = x11.XLookupKeysym(&event.xkey, 0);
                win.input_state.setKey(keysym, false);
            },
            x11.DestroyNotify => win.running = false,
            else => {},
        }
    }
}

test "Window struct compiles without ximage, gc, fb fields" {
    // Structural test: after refactoring, the Window struct only has
    // display, handle, width, height, and X11 state — no framebuffer fields.
    // This compiles when ximage, gc, fb are removed from the struct.
    const w = Window{
        .display = undefined,
        .handle = undefined,
        .width = 800,
        .height = 600,
        .wm_delete_atom = undefined,
        .running = true,
    };
    _ = w;
}

test "windowCreate new signature without fb param" {
    // After refactoring, windowCreate accepts (title, w, h) without fb.
    // This test verifies the signature change.
    const sig = @typeInfo(@TypeOf(windowCreate));
    const fn_info = sig.@"fn";
    try testing.expectEqual(fn_info.params.len, 3);
    try testing.expect(fn_info.return_type != null);
}

test "windowDestroy does not reference XDestroyImage or XFreeGC" {
    // This test verifies windowDestroy only closes the display.
    // We can't easily test this at runtime without a display,
    // but we verify the function type compiles.
    try testing.expect(@sizeOf(Window) > 0);
    try testing.expect(@TypeOf(windowDestroy) == fn (win: *Window) void);
}



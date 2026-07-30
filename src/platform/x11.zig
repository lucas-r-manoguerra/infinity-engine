//! Manual X11 FFI bindings.
//!
//! We declare exactly the X11 API surface we need instead of depending on
//! system headers. This is the "from scratch" way: zero header dependencies,
//! full control over the ABI boundary.

const std = @import("std");

// ---------------------------------------------------------------------------
// X11 Types (manual declarations)
// ---------------------------------------------------------------------------

pub const Display = opaque {};
pub const Window = c_ulong;
pub const GC = *anyopaque;
pub const Pixmap = c_ulong;
pub const Colormap = c_ulong;
pub const Cursor = c_ulong;
pub const Drawable = c_ulong;
pub const Font = c_ulong;
pub const Atom = c_ulong;
pub const KeySym = c_ulong;
pub const Time = c_ulong;
pub const XID = c_ulong;
pub const Bool = c_int;

pub const Visual = opaque {};
pub const Screen = opaque {};
pub const XIM = opaque {};
pub const XIC = opaque {};

pub const XEvent = extern union {
    type: c_int,
    xkey: XKeyEvent,
    xbutton: XButtonEvent,
    xmotion: XMotionEvent,
    xexpose: XExposeEvent,
    xclient: XClientMessageEvent,
    xconfigure: XConfigureEvent,
    xcrossing: XCrossingEvent,
    xfocus: XFocusChangeEvent,
    xkeymap: XKeymapEvent,
    xproperty: XPropertyEvent,
    xselection: XSelectionEvent,
    padding: [24]c_long,
};

pub const XKeyEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    window: Window,
    root: Window,
    subwindow: Window,
    time: Time,
    x: c_int,
    y: c_int,
    x_root: c_int,
    y_root: c_int,
    state: c_uint,
    keycode: c_uint,
    same_screen: Bool,
};

pub const XButtonEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    window: Window,
    root: Window,
    subwindow: Window,
    time: Time,
    x: c_int,
    y: c_int,
    x_root: c_int,
    y_root: c_int,
    state: c_uint,
    button: c_uint,
    same_screen: Bool,
};

pub const XMotionEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    window: Window,
    root: Window,
    subwindow: Window,
    time: Time,
    x: c_int,
    y: c_int,
    x_root: c_int,
    y_root: c_int,
    state: c_uint,
    is_hint: c_char,
    same_screen: Bool,
};

pub const XExposeEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    window: Window,
    x: c_int,
    y: c_int,
    width: c_int,
    height: c_int,
    count: c_int,
};

pub const XClientMessageEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    window: Window,
    message_type: Atom,
    format: c_int,
    data: extern union {
        bytes: [20]u8,
        shorts: [10]c_short,
        longs: [5]c_long,
    },
};

pub const XConfigureEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    event: Window,
    window: Window,
    x: c_int,
    y: c_int,
    width: c_int,
    height: c_int,
    border_width: c_int,
    above: Window,
    override_redirect: Bool,
};

pub const XCrossingEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    window: Window,
    root: Window,
    subwindow: Window,
    time: Time,
    x: c_int,
    y: c_int,
    x_root: c_int,
    y_root: c_int,
    mode: c_int,
    detail: c_int,
    same_screen: Bool,
    focus: Bool,
    state: c_uint,
};

pub const XFocusChangeEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    window: Window,
    mode: c_int,
    detail: c_int,
};

pub const XKeymapEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    window: Window,
    key_vector: [32]u8,
};

pub const XPropertyEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    window: Window,
    atom: Atom,
    time: Time,
    state: c_int,
};

pub const XSelectionEvent = extern struct {
    type: c_int,
    serial: c_ulong,
    send_event: Bool,
    display: *Display,
    requestor: Window,
    selection: Atom,
    target: Atom,
    property: Atom,
    time: Time,
};

pub const XSetWindowAttributes = extern struct {
    background_pixmap: Pixmap,
    background_pixel: c_ulong,
    border_pixmap: Pixmap,
    border_pixel: c_ulong,
    bit_gravity: c_int,
    win_gravity: c_int,
    backing_store: c_int,
    backing_planes: c_ulong,
    backing_pixel: c_ulong,
    save_under: Bool,
    event_mask: c_long,
    do_not_propagate_mask: c_long,
    override_redirect: Bool,
    colormap: Colormap,
    cursor: Cursor,
};

pub const XImage = extern struct {
    width: c_int,
    height: c_int,
    xoffset: c_int,
    format: c_int,
    data: ?*u8,
    byte_order: c_int,
    bitmap_unit: c_int,
    bitmap_bit_order: c_int,
    bitmap_pad: c_int,
    depth: c_int,
    bytes_per_line: c_int,
    bits_per_pixel: c_int,
    red_mask: c_ulong,
    green_mask: c_ulong,
    blue_mask: c_ulong,
    obdata: ?*anyopaque,
    f: XImageFuncs,
};

pub const XImageFuncs = extern struct {
    create_image: ?*anyopaque,
    destroy_image: ?*anyopaque,
    get_pixel: ?*anyopaque,
    put_pixel: ?*anyopaque,
    sub_image: ?*anyopaque,
    add_pixel: ?*anyopaque,
};

pub const XWindowAttributes = extern struct {
    x: c_int,
    y: c_int,
    width: c_int,
    height: c_int,
    border_width: c_int,
    depth: c_int,
    visual: *Visual,
    root: Window,
    class: c_int,
    bit_gravity: c_int,
    win_gravity: c_int,
    backing_store: c_int,
    backing_planes: c_ulong,
    backing_pixel: c_ulong,
    save_under: Bool,
    colormap: Colormap,
    map_installed: Bool,
    map_state: c_int,
    all_event_masks: c_long,
    your_event_mask: c_long,
    do_not_propagate_mask: c_long,
    override_redirect: Bool,
    screen: *Screen,
};

// ---------------------------------------------------------------------------
// Event type constants
// ---------------------------------------------------------------------------

pub const KeyPress = 2;
pub const KeyRelease = 3;
pub const ButtonPress = 4;
pub const ButtonRelease = 5;
pub const MotionNotify = 6;
pub const EnterNotify = 7;
pub const LeaveNotify = 8;
pub const FocusIn = 9;
pub const FocusOut = 10;
pub const KeymapNotify = 11;
pub const Expose = 12;
pub const GraphicsExpose = 13;
pub const NoExpose = 14;
pub const VisibilityNotify = 15;
pub const CreateNotify = 16;
pub const DestroyNotify = 17;
pub const UnmapNotify = 18;
pub const MapNotify = 19;
pub const MapRequest = 20;
pub const ReparentNotify = 21;
pub const ConfigureNotify = 22;
pub const ConfigureRequest = 23;
pub const GravityNotify = 24;
pub const ResizeRequest = 25;
pub const CirculateNotify = 26;
pub const CirculateRequest = 27;
pub const PropertyChange = 28;
pub const SelectionClear = 29;
pub const SelectionRequest = 30;
pub const SelectionNotify = 31;
pub const ColormapNotify = 32;
pub const ClientMessage = 33;
pub const MappingNotify = 34;
pub const GenericEvent = 35;
pub const LASTEvent = 36;

// ---------------------------------------------------------------------------
// Key symbols (extracted from X11/keysymdef.h)
// ---------------------------------------------------------------------------

pub const Key_Escape = 0xFF1B;
pub const Key_Return = 0xFF0D;
pub const Key_Tab = 0xFF09;
pub const Key_BackSpace = 0xFF08;
pub const Key_Delete = 0xFFFF;
pub const Key_Left = 0xFF51;
pub const Key_Up = 0xFF52;
pub const Key_Right = 0xFF53;
pub const Key_Down = 0xFF54;
pub const Key_Shift_L = 0xFFE1;
pub const Key_Shift_R = 0xFFE2;
pub const Key_Control_L = 0xFFE3;
pub const Key_Control_R = 0xFFE4;
pub const Key_Alt_L = 0xFFE9;
pub const Key_Alt_R = 0xFFEA;
pub const Key_Space = 0x0020;

// ---------------------------------------------------------------------------
// Event masks
// ---------------------------------------------------------------------------

pub const NoEventMask: c_long = 0;
pub const KeyPressMask: c_long = 1 << 0;
pub const KeyReleaseMask: c_long = 1 << 1;
pub const ButtonPressMask: c_long = 1 << 2;
pub const ButtonReleaseMask: c_long = 1 << 3;
pub const EnterWindowMask: c_long = 1 << 4;
pub const LeaveWindowMask: c_long = 1 << 5;
pub const PointerMotionMask: c_long = 1 << 6;
pub const ExposureMask: c_long = 1 << 15;
pub const StructureNotifyMask: c_long = 1 << 17;
pub const FocusChangeMask: c_long = 1 << 21;
pub const PropertyChangeMask: c_long = 1 << 22;

// ---------------------------------------------------------------------------
// Window attributes
// ---------------------------------------------------------------------------

pub const InputOutput: c_int = 1;
pub const InputOnly: c_int = 2;
pub const CopyFromParent: c_int = 0;
pub const ParentRelative: c_ulong = 1;
pub const InputOutputMask: c_ulong = 0;
pub const CWBackPixmap: c_ulong = 1 << 0;
pub const CWBackPixel: c_ulong = 1 << 1;
pub const CWBorderPixel: c_ulong = 1 << 2;
pub const CWEventMask: c_ulong = 1 << 11;

// ---------------------------------------------------------------------------
// XImage constants
// ---------------------------------------------------------------------------

pub const XYBitmap: c_int = 0;
pub const XYPixmap: c_int = 1;
pub const ZPixmap: c_int = 2;
pub const AllPlanes: c_ulong = ~@as(c_ulong, 0);

// ---------------------------------------------------------------------------
// Map state
// ---------------------------------------------------------------------------

pub const IsUnmapped: c_int = 0;
pub const IsUnviewable: c_int = 1;
pub const IsViewable: c_int = 2;

// ---------------------------------------------------------------------------
// X11 Functions
// ---------------------------------------------------------------------------

pub extern "X11" fn XOpenDisplay(display_name: ?*const u8) callconv(.c) ?*Display;
pub extern "X11" fn XCloseDisplay(display: *Display) callconv(.c) c_int;
pub extern "X11" fn XDefaultScreen(display: *Display) callconv(.c) c_int;
pub extern "X11" fn XDefaultVisual(display: *Display, screen_number: c_int) callconv(.c) *Visual;
pub extern "X11" fn XRootWindow(display: *Display, screen_number: c_int) callconv(.c) Window;
pub extern "X11" fn XDefaultDepth(display: *Display, screen_number: c_int) callconv(.c) c_int;
pub extern "X11" fn XCreateWindow(
    display: *Display,
    parent: Window,
    x: c_int,
    y: c_int,
    width: c_uint,
    height: c_uint,
    border_width: c_uint,
    depth: c_int,
    class: c_uint,
    visual: *Visual,
    valuemask: c_ulong,
    attributes: *XSetWindowAttributes,
) callconv(.c) Window;
pub extern "X11" fn XDestroyWindow(display: *Display, w: Window) callconv(.c) c_int;
pub extern "X11" fn XMapWindow(display: *Display, w: Window) callconv(.c) c_int;
pub extern "X11" fn XStoreName(display: *Display, w: Window, window_name: [*:0]const u8) callconv(.c) c_int;
pub extern "X11" fn XSelectInput(display: *Display, w: Window, event_mask: c_long) callconv(.c) c_int;
pub extern "X11" fn XNextEvent(display: *Display, event: *XEvent) callconv(.c) c_int;
pub extern "X11" fn XPending(display: *Display) callconv(.c) c_int;
pub extern "X11" fn XCreateImage(
    display: *Display,
    visual: *Visual,
    depth: c_uint,
    format: c_int,
    offset: c_int,
    data: ?*u8,
    width: c_uint,
    height: c_uint,
    bitmap_pad: c_int,
    bytes_per_line: c_int,
) callconv(.c) ?*XImage;
pub extern "X11" fn XPutImage(
    display: *Display,
    d: Drawable,
    gc: GC,
    image: *XImage,
    src_x: c_int,
    src_y: c_int,
    dest_x: c_int,
    dest_y: c_int,
    width: c_uint,
    height: c_uint,
) callconv(.c) c_int;
pub extern "X11" fn XDestroyImage(image: *XImage) callconv(.c) c_int;
pub extern "X11" fn XInternAtom(display: *Display, atom_name: [*:0]const u8, only_if_exists: Bool) callconv(.c) Atom;
pub extern "X11" fn XSetWMProtocols(display: *Display, w: Window, protocols: [*]Atom, count: c_int) callconv(.c) c_int;
pub extern "X11" fn XFree(data: *anyopaque) callconv(.c) c_int;
pub extern "X11" fn XFlush(display: *Display) callconv(.c) c_int;
pub extern "X11" fn XSync(display: *Display, discard: Bool) callconv(.c) c_int;
pub extern "X11" fn XCreateGC(
    display: *Display,
    d: Drawable,
    valuemask: c_ulong,
    values: ?*anyopaque,
) callconv(.c) ?GC;
pub extern "X11" fn XFreeGC(display: *Display, gc: GC) callconv(.c) c_int;
pub extern "X11" fn XLookupKeysym(key_event: *const XKeyEvent, index: c_int) callconv(.c) KeySym;
pub extern "X11" fn XGetWindowAttributes(display: *Display, w: Window, attrs: *XWindowAttributes) callconv(.c) c_int;
pub extern "X11" fn XDefaultColormap(display: *Display, screen_number: c_int) callconv(.c) Colormap;
pub extern "X11" fn XAllPlanes() callconv(.c) c_ulong;
pub extern "X11" fn XBlackPixel(display: *Display, screen_number: c_int) callconv(.c) c_ulong;
pub extern "X11" fn XWhitePixel(display: *Display, screen_number: c_int) callconv(.c) c_ulong;
pub extern "X11" fn XCreateSimpleWindow(
    display: *Display,
    parent: Window,
    x: c_int,
    y: c_int,
    width: c_uint,
    height: c_uint,
    border_width: c_uint,
    border: c_ulong,
    background: c_ulong,
) callconv(.c) Window;

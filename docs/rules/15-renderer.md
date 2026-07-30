# Renderer Backend Abstraction Rules

## Interface/Backend Architecture
```
Renderer (public interface, renderer/renderer.zig)
  ├─ Backend (tagged union — renderer.zig)
  │  ├─ .software → SoftwareBackend (renderer/software.zig)
  │  └─ .vulkan  → VulkanBackend   (renderer/vulkan.zig)
  └─ (future) OpenGL / Metal / WebGPU backends
```

## Renderer Interface Contract
The `Backend` type is a `union(enum)` with one variant per backend.
Every variant MUST implement these methods:

```zig
pub const Backend = union(enum) {
    software: SoftwareBackend,
    vulkan: VulkanBackend,

    /// Allocate and initialize backend resources.
    /// Call after window is created — backend needs the window handle.
    pub fn init(allocator: Allocator, window: *Window, width: u32, height: u32, comptime variant: Tag) !Backend;

    /// Submit a frame to the display (XPutImage or vkQueuePresentKHR).
    pub fn present(self: *Backend) void;

    /// Free all backend resources (walks partial-init state safely).
    pub fn deinit(self: *Backend) void;

    // --- Render operations ---

    /// Clear the framebuffer / begin render pass.
    pub fn beginFrame(self: *Backend, color: Color) void;

    /// End render pass / finalise the frame.
    pub fn endFrame(self: *Backend) void;

    /// Draw a textured triangle.
    pub fn drawTriangle(self: *Backend, v0: Vertex, v1: Vertex, v2: Vertex, texture: []const u8) void;
};
```

Key changes:
- **`init`** receives the window pointer directly — backends extract X11 display/handle from it.
  No separate `RenderConfig` struct is passed; width and height are individual params.
- **`present`** takes no extra arguments — the backend owns all presentation resources.
- **`getFramebuffer` removed** — backends do not expose their framebuffer externally.

## Backend Selection Rules
- Backend selected at ENGINE INIT (runtime, not compile time)
- Vulkan tried first; software used as guaranteed fallback
- Backend is set once and immutable for the engine lifetime
- No hot-swap between backends — initialize the right one at the start

```zig
// ✅ Runtime selection — try Vulkan, fallback to Software
const backend = Backend.init(allocator, &win, width, height, .vulkan) catch |err| blk: {
    std.debug.print("Vulkan unavailable ({}), using software\n", .{err});
    break :blk try Backend.init(allocator, &win, width, height, .software);
};
```

## Window Lifecycle
- Window is created **first** → backends receive `*Window` during init
- Window is destroyed **last** (after backend deinit)
- Backend owns presentation resources; window only manages the X11 connection

```
engine.init:
  1. windowCreate(title, w, h) → Window
  2. Backend.init(allocator, &win, w, h, .vulkan) → Backend  (or .software)

engine.run (per frame):
  1. beginFrame(color)     → clear / begin render pass
  2. ecs runSystems(.render)
  3. endFrame()            → finish frame
  4. present()             → XPutImage / vkQueuePresentKHR

engine.deinit:
  1. Backend.deinit()      → free GPU/CPU resources
  2. windowDestroy(&win)   → close display
```

## Backend-Specific Rules

### Software Backend
- Framebuffer: BGRA32, allocated in `init`, presented via XPutImage in `present()`
- Transform pipeline: fully CPU (no shader compilation)
- Owns X11 GC, created at init time, freed at deinit
- For MVP only — no new features after M2

### Vulkan Backend
- All Vulkan calls behind the `vulkan.zig` abstraction (`vk.zig`, `vk_loader.zig`)
- Pipeline cache for faster startup on second run
- No raw Vulkan handles in game code
- Init state machine walk: none → instance → surface → device → swapchain → pipeline → ready
- `deinit` walks backward from current state, safe after partial init failure

## Prohibited
- ❌ Backend-specific types leaking into game/renderer interface
- ❌ Conditional `if software ... else if vulkan ...` outside the backend files
- ❌ Adding a new backend without implementing ALL interface functions
- ❌ Backend selection based on `comptime` OS checks (use runtime feature detection)
- ❌ Storing `*Window` pointer from init — the Engine owns the window lifecycle

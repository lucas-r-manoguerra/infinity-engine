# Platform Abstraction Rules

## Interface/Backend Split
- `platform/*.zig` defines the PUBLIC interface
- `platform/*_[backend].zig` implements for a specific OS
- Engine code NEVER imports a backend directly — only through the interface

## Required Abstractions
| Abstraction | Status | Backends |
|---|---|---|
| Window | ✅ Active | X11 (Linux) |
| Input | ✅ Active | X11 keyboard |
| Render Surface 🔲 | Planned | Vulkan surface, software FB |
| File System 🔲 | Planned | POSIX, Win32 |
| Threading 🔲 | Planned | pthread, Win32 threads |
| Audio 🔲 | Planned | ALSA, WASAPI, CoreAudio |

## Rules
- No `comptime` platform branching outside `platform/`
- Feature detection at runtime, not compile-time (e.g., "has Vulkan?", not "is Windows?")
- Platform backends are selected at build time via `build.zig`, not `comptime if`
- Every platform abstraction has exactly one active backend at compile time
- Non-implemented backends return `error.NotSupported` (graceful, not crash)

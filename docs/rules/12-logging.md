# Logging & Diagnostics Rules

## Log System
- Single logging interface in `core/log.zig`
- Log levels: `DEBUG`, `INFO`, `WARN`, `ERROR`
- In RELEASE builds: `DEBUG` and `INFO` are compiled out (zero cost)
- Logs are STRUCTURED (level + subsystem + message + optional payload), not free-text
- Log output goes to stderr, never stdout (stdout is for the game)

```zig
// ✅ Structured API
log.debug(.ecs, "entity created: {d}", .{entity.id});
log.error(.renderer, "vulkan device lost: {s}", .{err});
```

## Compile-Time Stripping
| Level | Debug | ReleaseSafe | ReleaseFast |
|---|---|---|---|
| DEBUG | ✅ | 🔲 stripped | 🔲 stripped |
| INFO | ✅ | ✅ | 🔲 stripped |
| WARN | ✅ | ✅ | ✅ |
| ERROR | ✅ | ✅ | ✅ |

## Diagnostics
- Each subsystem exposes a `Diagnostics` struct with counters
- Frame-level diagnostics: gather → report → reset at frame end
- Diagnostics NEVER allocate — atomic increments on integers
- In-game overlay (debug only): frame time, FPS, entity count, allocator high-water

```zig
pub const Diagnostics = struct {
    entities_alive: u32 = 0,
    entities_created: u32 = 0,
    entities_destroyed: u32 = 0,
    systems_ran: u32 = 0,
    ms_frame: f32 = 0,
    ms_ecs: f32 = 0,
    ms_render: f32 = 0,
    allocator_high_water: usize = 0,
};
```

## Rules
- ❌ `std.debug.print` outside of throwaway prototype code
- ❌ String formatting in hot paths (even in debug)
- ❌ Logging sensitive data (file paths, user input) in release
- ✅ Use comptime log level stripping — zero overhead for disabled levels

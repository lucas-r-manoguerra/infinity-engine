# Code Style Rules

## Naming
| Category | Convention | Example |
|---|---|---|
| Types | PascalCase | `Vec3`, `EntityHandle`, `GameLoop` |
| Functions | camelCase | `translate`, `init`, `deinit` |
| Files | snake_case | `vec3.zig`, `game_loop.zig` |
| Constants | SCREAMING_SNAKE_CASE | `MAX_ENTITIES`, `FIXED_DT` |
| Private fields | leading `_` | `_internal_state` |
| Error sets | PascalCase | `EngineError`, `RenderError` |

## File Structure (strict order)
```zig
//! Doc comment: what this file owns

const std = @import("std");
const Internal = @import("internal.zig");

/// Module-level constants
const MAX_ITEMS = 256;

/// Public types — exported first
pub const MyType = struct {
    field: u32,
    pub fn init() MyType { ... }
};

// Private helpers — never `pub` unless needed by tests
fn helper() void { ... }

// Allocator parameter naming — always `allocator`
pub fn create(allocator: std.mem.Allocator) void { ... }

// Tests — always at the bottom
test "behavior description" { ... }
```

## Imports
- Internal imports: relative paths (`@import("math/vec3.zig")`)
- Standard library: `@import("std")`
- Never `@import` by package name for internal modules

## Prohibited
- ❌ Global/static mutable state (`var` at module level)
- ❌ `usingnamespace` (deprecated in Zig 0.14+)
- ❌ Files over 200 lines
- ❌ Type erasure via `@ptrCast` where alternatives exist

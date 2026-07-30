# API Visibility Rules

## Export Tiers

### `pub` — Public API (stable)
- Everything marked `pub` is a commitment
- Breaking changes require deprecation cycle of 2 milestones
- Must be documented (`//!` module doc or `///` doc comment on the binding)
- Must have tests

### `pub` with `//@experimental` — Unstable Preview
- May change without deprecation
- Still tested, still documented
- Used for features in development that need public testers
- Automatically deprecated after 2 milestones or promoted to stable

```zig
/// Returns a handle to the new entity.
///@experimental — API may change in M3
pub fn spawnEntity(world: *World) Entity { ... }
```

### `test` scope — Test-only visibility
- Functions used exclusively by tests are `pub` but tagged `//@test-helper`
- Never used by production code
- Not part of the API contract

```zig
/// Resets the world state for testing.
//@test-helper
pub fn resetForTesting(world: *World) void { ... }
```

### Private (no `pub`) — Internal implementation
- Anything not `pub` is internal and can change at any time
- No documentation commitment
- No test requirement (but still recommended for complex logic)

## Deprecation Protocol
1. Add `comptime` deprecation warning via `@compileLog` with migration hint
2. Keep the old API for 2 milestones
3. Remove in the third milestone

```zig
pub const OldFunction = struct {
    comptime {
        @compileLog("OldFunction is deprecated, use NewFunction instead");
    }
    // ... implementation forwarding to new API
};
```

## Prohibited
- ❌ `pub` without documentation
- ❌ Breaking changes to `pub` API without deprecation cycle
- ❌ Using `//@test-helper` functions in production code
- ❌ Making internal functions `pub` "just in case" — start private, widen if needed

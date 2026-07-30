# Memory Rules

## Explicit Allocators — HARD RULE
- Every subsystem that allocates receives its allocator explicitly
- Zero implicit or global allocators
- Passing allocator as parameter preferred over storing it

## Allocation Patterns
| Scope | Allocator | Use |
|---|---|---|
| Per-frame | Arena `ArenaAllocator` | Temp data, reset every frame end |
| Engine lifetime | Arena `ArenaAllocator` | Persistent data, freed at shutdown |
| Fixed-size objects | Pool | Entities, components, nodes |
| General purpose | `GeneralPurposeAllocator` | Unpredictable sizes, startup |

## Arena Discipline
- Frame arena: reset at END of each frame, never mid-frame
- No individual frees on arena memory — reset is the free
- Nested allocators: child receives parent arena or gpalloc

## What NOT to Do
- ❌ `std.heap.page_allocator` outside minimal bootstrapping
- ❌ Global `var` storing an allocator handle
- ❌ Allocating in hot paths without pre-allocated pools
- ❌ Forgetting to reset per-frame arenas (memory bleed each frame)

## Verification
- Tests run with `CheckedAllocator` wrapping the real allocator
- `zig build test` reports 0 leaks or the PR is rejected
- Frame allocator: log high-water mark per frame in debug builds

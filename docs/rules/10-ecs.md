# ECS Design Rules

## Component Rules
- Components are DATA ONLY — no methods beyond `init`/`deinit` (if they allocate)
- No inheritance, no interfaces, no tagged unions in components
- Max ~64 bytes per component (cache line awareness)
- Components are plain structs with `comptime` registration, no runtime type info

```zig
// ✅ Good
pub const Health = struct {
    current: f32,
    max: f32,
};

// ❌ Bad — logic in the component
pub const Health = struct {
    current: f32,
    max: f32,
    pub fn heal(self: *Health, amount: f32) void { ... }
};
```

## System Rules
- One system = one concern (split rendering vs physics, don't mix)
- Systems receive their dependencies explicitly, never query World from inside
- System execution order is defined by phase, not implicit iteration
- Systems do NOT hold state — state lives in components or resources

## Query Rules
- Queries iterate over matching archetypes, never scan all entities
- Add/remove component during iteration is queued, not immediate
- Filter by component set (ALL of A, B, C), not by name strings

## Entity Lifecycle
- Entities are handles (u32 index + generation), never pointers
- Destroyed entities increment generation (safety against dangling handles)
- Entity handle is NOT guaranteed valid after a deferred command flush

## Prohibited
- ❌ Direct access to `World` internals from systems
- ❌ Systems calling other systems directly (use phases and ordering)
- ❌ Storing entity handles across frames (they can be recycled)
- ❌ Components containing allocated memory without `deinit`

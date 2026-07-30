# Testing Rules

## Mandatory Coverage
- Every `pub fn` MUST have at least one test
- Exception: pure data-only structs with no logic
- Tests live in `tests/` mirroring `src/` structure exactly
  - `src/math/vec3.zig` → `tests/math/vec3_test.zig`
  - `src/ecs/world.zig` → `tests/ecs/world_test.zig`

## Test Quality
- Test names describe BEHAVIOR, not implementation
  - ✅ `test "vec3 add produces correct result"`
  - ❌ `test "test_add"`
- One logical assertion per test
- Use `std.testing` only — zero external test dependencies
- Arena allocator for test-scoped allocations (no manual frees)
- Verify no leaks: `zig build test` MUST report 0 leaks

## Enforcement
- `zig build test` MUST pass before any commit
- No commit without green tests. Period.
- Failing tests block PRs. Flaky tests are bugs.

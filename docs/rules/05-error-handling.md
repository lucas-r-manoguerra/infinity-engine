# Error Handling Rules

## Pattern
- Zig error sets for recoverable errors
- `try` and `catch` for propagation — never `catch unreachable` unless provably infallible
- Engine-wide shared errors in `core/error.zig`

## Error Categories
| Category | Mechanism | Example |
|---|---|---|
| Recoverable | Return error set | `error.FileNotFound`, `error.InitFailed` |
| Programming error | `@panic` / `std.debug.panic` | Null invariant, bounds violation |
| External failure | Return error + payload | `error.AllocationFailed`, `error.WindowCreateFailed` |

## Rules
- ❌ Never `catch null` — hidden errors become silent failures
- ❌ Never `catch unreachable` on external operations (file IO, window, alloc)
- ✅ Public API error sets: explicit, not inferred (`pub const Error = error{...}`)
- ✅ Return specific errors, not generic `error.Failure`
- ✅ Wrap third-party errors into project error sets at the boundary

## Error Sets
- Each subsystem defines its own error set
- Subsystem errors compose upward: `EngineError = alloc.Error || Window.Error`
- Public functions document which errors they return

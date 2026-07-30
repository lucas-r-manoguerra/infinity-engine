# Architecture Rules

## Layer Boundaries
- **Low-level modules MUST NOT import high-level modules.**
- `math/`, `core/` are foundation — they know nothing about `ecs/`, `renderer/`, `ai/`, `blueprint/`
- `ecs/` knows `core/` and `math/` but NOT `renderer/`, `ai/`, `blueprint/`
- `renderer/` knows `core/` and `math/` but NOT `ecs/`, `ai/`, `blueprint/`
- `ai/` communicates ONLY through serialized `ContextSnapshot` — never imports subsystems directly
- `blueprint/` is independent, knows only `core/`

## Dependency Direction
```
math/        → (nothing, zero deps)
core/        → math/
platform/    → OS APIs only (X11, Win32, Cocoa)
ecs/         → core/, math/
renderer/    → core/, math/, platform/
ai/          → core/ (ContextSnapshot only)
blueprint/   → core/
runtime/     → everything (orchestrates subsystems)
```

## File Boundaries
- One file = exactly one responsibility
- Max ~200 lines per file. If exceeded, SPLIT.
- Exception: thin re-export files (<30 lines)
- Tests mirror src/ structure exactly

## Enforcement
- Violations are caught in code review, not CI
- If a low-level file imports a high-level module, the PR is rejected

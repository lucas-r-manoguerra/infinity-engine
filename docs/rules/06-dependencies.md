# Dependencies & Build Rules

## Zero External Dependencies (MVP)
- MVP permits ONLY system libraries available on the target OS:
  - Linux: X11, dl, Vulkan SDK (runtime-loaded)
- No packages from registries (Zig package manager, git submodules)
- All engine functionality is self-implemented

## Dependency Justification (Post-MVP)
Before adding ANY external dependency, answer:

1. Does it solve a real, well-understood problem we HAVE NOW?
2. Can we write it ourselves with less long-term maintenance burden?
3. Is the dependency stable, actively maintained, and permissively licensed?
4. Does it add >100KB to the binary? (if yes, seek alternatives)
5. Does it introduce a new runtime dependency? (if yes, must be dlopen-ed)

One "no" = the dependency is rejected.

## Build Configuration
| Mode | Flags | Use |
|---|---|---|
| Debug | `zig build` | Development, safety enabled |
| Release | `zig build -Doptimize=ReleaseSafe` | Testing, staging |
| Dist | `zig build -Doptimize=ReleaseFast` | Shipping |

## CI Gates
- `zig build test` — zero failures, zero leaks
- `zig build` — zero compiler warnings
- No warnings with `@compileLog` or dead code

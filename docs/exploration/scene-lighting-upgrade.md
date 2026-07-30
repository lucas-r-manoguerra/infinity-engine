# Exploration: Scene Complexity & Lighting Upgrade

## Current State

### Scene Stats
| Metric | Value |
|--------|-------|
| Resolution | 800 × 600 |
| Entities (cubes) | 7 (1 center + 5 orbiting + 1 moon) |
| Triangles per cube | 12 (2 per face × 6 faces) |
| Total triangles/frame | 84 |
| Texture size | 64 × 64 BGRA32 |
| Framebuffer | 800 × 600 × 4 = ~1.9 MB |
| Z-buffer | 800 × 600 × 4 = ~1.9 MB (f32) |
| Max entities (ECS capacity) | 1,000 |
| Max component types | 64 |
| Frame budget target | 16.6 ms (60 FPS) |
| Current render time | ~1.7 ms ReleaseSafe (84 tris) |

### Lighting
- **Zero lighting code exists.** No normals, no light direction, no shading calculations.
- Vertex colors are **hardcoded per-face** in `renderCube()` — 36 distinct `Color` values hand-picked to suggest shading (faces get different RGB values).
- `modulateColor()` in `color.zig` multiplies vertex color × texture color per channel.
- The `sampleTexture()` function samples a 64×64 checkerboard texture.

### Vertex Format
```zig
pub const Vertex = struct {
    x: f32, y: f32, z: f32,   // clip-space position
    u: f32, v: f32,            // texture coords (pre-divided by w)
    inv_w: f32,                // 1/w for perspective correction
    color: Color,              // packed u32: b, g, r, a
};
```
- **No normal**, **no tangent**, no additional attributes.
- `Color` is a `packed struct(u32)` with BGRA byte order.
- The rasterizer barycentrically interpolates R, G, B, U, V, Z.

### Software Rasterizer
- Barycentric edge-function rasterizer with **DDA attribute interpolation** per scanline.
- Z-buffer for occlusion.
- Back-face culling done at the CPU level in `renderCube()` (area < 0 check).
- `drawTriangle()` receives 3 `Vertex` and a `texture: []const u8`.
- Per-pixel cost: sample Z, compare, compute RGB (from interpolated + texture modulate), write pixel.
- Every pixel in the bounding box is tested — no hierarchical/tiled rasterization.
- The user's ~1.7 ms render time for 84 tris at 800×600.

### ECS Architecture
- **Entity**: 64-bit handle (u32 index + u32 generation).
- **Component storage**: Sparse set per component type — O(1) add/get/remove.
- **Query**: Linear scan of entity_pool (up to max_entities) checking component presence. NOT an archetype-based query. O(max_entities × component_count) worst case.
- **System phases**: `pre_update`, `update`, `post_update`, `render`.
- **Systems**: function pointer with `*anyopaque` context.
- **Current components**: `Transform` (x,y,z,rot_x,rot_y,scale), `RotationSpeed`, `Orbit`, `Parent`.

### Transform in Engine
```zig
const Transform = struct { x: f32, y: f32, z: f32, rot_x: f32, rot_y: f32, scale: f32 = 1.0 };
```
- Position is raw x/y/z (not a Vec3).
- Rotation is two scalar floats (Euler), not a quaternion.
- Scale is uniform float.
- `renderCube()` builds MVP by computing model matrix from these.

### Test Infrastructure
- `zig build test` via `src/test_runner.zig` — imports all modules.
- Tests use `std.testing` only. ~90 inline tests across math, ECS, core, platform, renderer.
- No tests in `tests/` directory (rules say they should mirror src/ but current practice has them inline).
- `zig build bench` runs 4 benchmark suites in ReleaseSafe.
- No leak checking currently (no CheckedAllocator wrapper in tests).

---

## Affected Areas

### Core (change-safe)
| File | Responsibility |
|------|---------------|
| `src/core/color.zig` | `Color` struct, `COLORS` constants, `modulateColor()` |
| `src/core/diagnostics.zig` | Per-frame phase timing, benchmark reporting |
| `src/core/error.zig` | Error sets |
| `src/core/time.zig` | nanoTime, DeltaTime, FpsCounter |

### Math (change-safe)
| File | Responsibility |
|------|---------------|
| `src/math/vec3.zig` | Vec3: add, sub, dot, cross, length, normalize, lerp |
| `src/math/mat4.zig` | Mat4: mul, transformPoint, perspective, lookAt, translate, rotateX/Y/Z, scale, inverse |
| `src/math/quat.zig` | Quat: mul, rotate, normalize, slerp, fromAxisAngle |
| `src/math/transform.zig` | Transform struct with toMatrix(), lookAt(), forward(), etc. |

### ECS (change-safe)
| File | Responsibility |
|------|---------------|
| `src/ecs/world.zig` | World: entity lifecycle, component register/add/get/remove, system execution |
| `src/ecs/entity.zig` | Entity handle (u64) |
| `src/ecs/component.zig` | ComponentRegistry, ComponentStorage (sparse set) |
| `src/ecs/query.zig` | Query: linear entity scan with component filter |
| `src/ecs/system.zig` | System, SystemPhase, SystemGraph |

### Renderer (Vertex format changes ripple through ALL backends)
| File | Responsibility |
|------|---------------|
| `src/renderer/renderer.zig` | **Vertex struct**, Backend tagged union dispatch |
| `src/renderer/software.zig` | Barycentric rasterizer, attribute interpolation, pixel write |
| `src/renderer/vulkan.zig` | Vulkan backend (incomplete/preview — drawTriangle is a no-op) |

### Runtime (scene composition)
| File | Responsibility |
|------|---------------|
| `src/runtime/engine.zig` | **Engine struct, game loop, ECS init, renderCube(), all system definitions, Transform/RotationSpeed/Orbit/Parent components** |
| `src/main.zig` | Entry point, sets WIDTH=800, HEIGHT=600 |

### Platform (no changes needed)
| File | Responsibility |
|------|---------------|
| `src/platform/window.zig` | X11 window create/destroy/poll |
| `src/platform/input.zig` | Input state + action mapping |

---

## What Can Change vs What's Fixed

### CAN change (easy, no cascade)
- Add new ECS components (Light, Material, Mesh, etc.) — up to 64 total
- Add new entities and shapes in `postInit()`
- Change scene complexity (cube count, positions, etc.)
- Change resolution (WIDTH/HEIGHT in `main.zig`)
- Change texture size or pattern in `software.zig`
- Add lighting computation code (new file or inline in render system)
- Add new utility functions to color.zig

### REQUIRES multi-backend awareness (3 files minimum)
- **Vertex format changes**: Adding `nx`, `ny`, `nz` (or any field) to `Vertex` requires:
  1. `renderer.zig` — struct definition
  2. `software.zig` — add barycentric interpolation of new attributes in `drawTriangle()`
  3. `vulkan.zig` — update shader input layout, though Vulkan backend is currently a preview/stub

### FIXED / hard to change
- **X11** is the only window system
- **ECS max entities**: 1,000 (set at World.init)
- **ECS max components**: 64 (ComponentRegistry limit)
- **Query is linear scan** — no archetype-based ECS (would be a major refactor)
- **Software rasterizer is scalar** — no SIMD, no tiling, no multithreading
- **No GPU** — all rendering is CPU
- **Vulkan backend is incomplete** — can't test Vulkan path for lighting changes

---

## Features We Want to Add

### F1: Per-Vertex Normals + Directional Lighting
**What**: Add `nx, ny, nz` to Vertex, compute face normals in `renderCube()`, interpolate in rasterizer, compute Lambertian diffuse term per pixel.

**Subsystems touched**:
- `renderer.zig` — Vertex struct (add nx, ny, nz: f32)
- `software.zig` — barycentric interpolation of (nx, ny, nz), lighting math in inner pixel loop
- `vulkan.zig` — update stub to match new Vertex (trivial, it's a no-op)
- `engine.zig` — precompute normals for each triangle face, pass to drawTriangle
- `color.zig` or new `src/renderer/lighting.zig` — lighting utility (ambient + diffuse)

**Complexity**: **Medium** (~5 files, ~150-200 lines)
**Risk**: Low — normals default to (0,0,0) = no effect on existing visuals

### F2: Directional Light Resource (ECS)
**What**: Add a `DirectionalLight` component (direction + color + intensity) stored as a singleton on a dedicated light entity, read during the render phase.

**Subsystems touched**:
- `engine.zig` — register component, create light entity in postInit, pass light data to render system
- `ecs/world.zig` — already handles arbitrary components (no changes needed)
- `renderer.zig` / `software.zig` — use light direction in per-pixel lighting equation

**Complexity**: **Low** (~2 files, ~30-50 lines)
**Risk**: Minimal

### F3: Point Lights with Attenuation
**What**: Multiple point lights with position, color, radius, falloff. Render system loops over lights per entity, rasterizer sums contributions per pixel.

**Subsystems touched**:
- `engine.zig` — PointLight component, create light entities
- `renderer/software.zig` — per-pixel loop over active lights (significant perf impact)
- `core/color.zig` — optional color math helpers

**Complexity**: **Medium-High** (~3 files, ~100-150 lines)
**Risk**: Medium — N lights × M tris × pixels = potential frame time explosion. Must be bounded or light count capped.

### F4: Ambient Light Only
**What**: Simple ambient term (uniform light color) applied to all vertices/pixels — no direction, no calculations.

**Subsystems touched**:
- `engine.zig` — pass ambient color to render system
- `color.zig` or `software.zig` — modulate vertex color by ambient factor

**Complexity**: **Very Low** (~2 files, ~15-20 lines)
**Risk**: Near zero

### F5: Scene Complexity Increase
**What**: More cubes, planets, different shapes (spheres via triangle subdivision, pyramids).

**Subsystems touched**:
- `engine.zig` — new shape generators, more entities in postInit
- `math/vec3.zig` — already has everything needed
- ECS — World capacity already 1,000

**Complexity**: **Very Low** (~1 file, ~50-100 lines)
**Risk**: Performance degrades linearly with triangle count. 84→840 tris might push render from ~1.7ms to ~17ms.

### F6: Material Component
**What**: Factor out hardcoded per-face colors into a Material component (diffuse color, specular params, etc.).

**Subsystems touched**:
- `engine.zig` — Material struct, register component, assign to entities
- `ecs/` — no changes needed, component fits existing system

**Complexity**: **Low** (~2 files, ~60-80 lines)
**Risk**: Low — pure refactor of existing color data

---

## Performance Scaling Estimates

| Triangles | Est. render time (ReleaseSafe, 800×600) | Notes |
|-----------|----------------------------------------|-------|
| 84 (current) | ~1.7 ms | Measured baseline |
| 168 (2×) | ~3.0-3.5 ms | Scanline setup dominates per-triangle |
| 420 (5×) | ~7-8 ms | Still under 16.6 ms budget |
| 840 (10×) | ~14-17 ms | Hitting budget limit |
| 1,680 (20×) | ~30+ ms | Over budget — need optimization |

Adding per-pixel lighting will add ~1.5-2 ns per light per pixel (a few multiply-adds + a texture read). At 800×600 = 480K pixels with even 50% coverage ≈ 240K lit pixels, 1 directional light adds ~0.3-0.5 ms.

**Key insight**: The rasterizer cost scales with bounding-box area, not triangle count alone. Large triangles dominate. Small/far triangles are cheaper. Adding many small distant cubes is much cheaper than adding a few large ones.

---

## Recommended Feature Order

Phase 1 (minimum viable lighting):
1. **F4** (Ambient) + **F5** (more entities) — quick wins for visual improvement
2. **F1** (Per-vertex normals + directional light) — the core lighting feature

Phase 2 (richer scene):
3. **F6** (Material component) — organize the color data
4. **F2** (Directional light ECS component) — make light a first-class entity

Phase 3 (advanced):
5. **F3** (Point lights) — only after confirming frame budget headroom

## Risks

- **Vertex format change cascades** to the Vulkan backend (which is a stub, but the interface must compile).
- **Linear-scan queries** are O(max_entities) even when few entities have a component. With 500+ entities and frequent render queries, this adds ~0.1-0.2 ms.
- **Per-pixel lighting multiplies ALU cost** per covered pixel. Must profile after implementation to confirm headroom.
- **No automated leak detection** — `zig build test` doesn't wrap allocators in CheckedAllocator.
- **No performance regression gates** — bench results must be compared manually.

## Ready for Proposal

Yes — full exploration complete. Recommending Phase 1 (Ambient + Normals + Directional Light + More Entities) as the proposal scope.

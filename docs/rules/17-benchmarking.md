# Benchmarking Rules

## Principio
Los tests de correctness verifican que algo funciona. Los benchmarks verifican que funciona **lo suficientemente rápido**. En un game engine ambas son obligatorias — una regresión de performance es un bug.

## Hotspots Prioritarios
Solo se benchmarcan rutas que:
- Se ejecutan >10.000 veces por frame (math, ECS queries, transform chains)
- Tienen presupuesto de tiempo fijo (renderer, physics)
- Son críticas para el frame budget (<16.6ms total)

Prioridad actual:
| Prioridad | Módulo | Por qué |
|---|---|---|
| 🔴 P0 | `math/` (vec3, mat4, quat, transform) | Se llaman millones de veces, cualquier slowdown es catastrófico |
| 🔴 P0 | `ecs/query.zig` (iteration, filtering) | Determina cuántas entidades podemos tener |
| 🟡 P1 | `core/memory.zig` (arena alloc, pool alloc) | El costo de allocar define la arquitectura de memoria |
| 🟡 P1 | `renderer/software.zig` (triangle fill, clear) | El software renderer tiene un límite duro de resolución/complejidad |
| 🔵 P2 | `ecs/entity.zig` (create/destroy) | Operaciones frecuentes pero no por frame |
| ⚫ P3 | `ai/`, `blueprint/` | Son esqueletos — no benchmarkear hasta que tengan implementación real |

## Harness Mínimo

No existe `std.testing.Benchmark` en Zig. Usar este patrón:

```zig
const std = @import("std");

/// Run `func` for `iterations` times, print ns/op.
pub fn bench(comptime name: []const u8, iterations: u64, func: *const fn () void) void {
    const start = std.time.nanoTime();
    for (0..iterations) |_| func();
    const elapsed = std.time.nanoTime() - start;
    const ns_per_op = @as(f64, @floatFromInt(elapsed)) / @as(f64, @floatFromInt(iterations));
    std.debug.print("BENCH {s}: {d:.1} ns/op\n", .{ name, ns_per_op });
}
```

Para benchmarks que reciben contexto (allocator, escenario):

```zig
pub fn benchContext(comptime name: []const u8, iterations: u64, ctx: anytype, func: *const fn (@TypeOf(ctx)) void) void {
    const start = std.time.nanoTime();
    for (0..iterations) |_| func(ctx);
    const elapsed = std.time.nanoTime() - start;
    const ns_per_op = @as(f64, @floatFromInt(elapsed)) / @as(f64, @floatFromInt(iterations));
    std.debug.print("BENCH {s}: {d:.1} ns/op\n", .{ name, ns_per_op });
}
```

## Estructura

```
benchs/                     # Benchmarks al mismo nivel que src/ y tests/
├── math_bench.zig          # Vec3, Mat4, Quat, Transform benchmarks
├── ecs_bench.zig           # Query iteration, entity lifecycle
├── memory_bench.zig        # Arena allocator, pool allocator
├── renderer_bench.zig      # Software renderer hotspots
└── main.zig                # Benchmark runner (importa todos y los ejecuta)
```

## Build Integration

En `build.zig`:

```zig
const bench = b.addExecutable(.{
    .name = "infinity-engine-bench",
    .root_module = b.createModule(.{
        .root_source_file = b.path("benchs/main.zig"),
        .target = target,
        .optimize = .ReleaseSafe,  // ← ReleaseSafe, no Debug
        .link_libc = true,
    }),
});
bench.root_module.addObject(x11_bridge);
bench.root_module.linkSystemLibrary("dl", .{});

const bench_run = b.addRunArtifact(bench);
const bench_step = b.step("bench", "Run performance benchmarks (ReleaseSafe)");
bench_step.dependOn(&bench_run.step);
```

**Los benchmarks SIEMPRE corren en ReleaseSafe**. Debug build mide overhead del safety checks, no performance real.

## Gates

| Gate | Acción |
|---|---|
| PR que toca `math/` | Debe incluir resultado de bench antes/después |
| PR que toca `ecs/query.zig` | Debe incluir resultado de bench antes/después |
| Release candidate | Correr `zig build bench` completo, comparar contra tag anterior |
| Regresión >10% | Bloquea merge. O se optimiza o se justifica con tradeoff documentado |

Ejemplo de bloqueo:
```
⚠️ BENCH REGRESSION: mat4.mul 12.3 → 18.7 ns/op (+52%)
   Cause: align(16) removed from Mat4.data
   Fix: restore align(16) or justify in PR description
```

## Lo que NO se benchmarquea

- ❌ Código que no está en hot path (init, shutdown, config loading)
- ❌ Operaciones IO-bound (file loading, asset parsing)
- ❌ Esqueletos / stubs / código sin implementación real
- ❌ Lo que no se puede medir establemente en CI (GPU, red, timers no confiables)

## Mantenimiento

- Los thresholds de benchmark se actualizan cuando cambian los requerimientos (target de framerate, resolución, cantidad de entidades)
- Los benchmarks son tan importantes como los tests: si fallan, se investigan. No se ignoran.
- Agregar un benchmark nuevo es tan simple como escribir la función y registrarla en `main.zig`

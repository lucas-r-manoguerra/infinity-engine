# Regla 01 — Arquitectura

> La arquitectura es un contrato, no una sugerencia. CMake la hace cumplir en link.

## Capas y dirección de dependencia

```
apps/  →  infinity_runtime                    (F6, planificado)
runtime  →  core, platform, renderer, ecs     (F6, planificado)
renderer →  core, math, ecs (F5.12, planificado) (+ platform con backend Vulkan, F4.5)
ecs      →  core, math                        (F5, planificado)
platform →  core
ai       →  core            (F7, planificado — al resto vía ContextSnapshot serializado)
blueprint→  core            (F8, planificado)
assets   →  core, platform   (futuro F9: ADR-011/040, streaming 077, procgen 076)
network  →  core             (futuro F15: ADR-062 server authority, partición 073, interés 093)
math     →  (nada)
core     →  (nada)
```

**Reglas de oro:**

1. Un módulo **jamás** incluye headers de un módulo de nivel superior.
2. Cada módulo es un **static library target** (`infinity_math`, `infinity_core`, …).
   Si un módulo necesita a otro, lo linkea en su `CMakeLists.txt` con `target_link_libraries`.
3. Una dependencia ilegal no se discute: **rompe el build**.
4. `ai/` nunca importa ECS/Renderer directamente — recibe `ContextSnapshot` (data-only).
5. `platform/` define interfaces; los backends viven en `src/<backend>/` y se eligen en
   tiempo de compilación según la plataforma (headless hoy; X11 en Linux, Win32 en Windows,
   Cocoa en macOS según disponibilidad). El backend es agnóstico: multiplataforma por diseño.
6. Los módulos futuros (`assets` F9, `network` F15) entran a este contrato cuando
   arranca su fase; la dirección de dependencia se respeta igual. El contrato completo
   vive en `docs/ARCHITECTURE.md` (97 ADRs) — esta regla es el resumen operativo.

## Estructura de un módulo

```
engine/<mod>/
├── include/infinity/<mod>/   # headers públicos (lo único exportable)
├── src/                      # implementación
├── CMakeLists.txt            # target: infinity_<mod>
```

- Solo `include/` se propaga vía `target_include_directories` (PUBLIC).
- Lo que no está en `include/` no existe para los demás módulos.
- Tests en `tests/<mod>/`, NUNCA dentro del módulo.

## One File = One Task

Cada archivo = una responsabilidad. Límite: **~300 líneas**. Al pasarlo, partir.

✅ `engine/math/include/infinity/math/vec3.h` — solo Vec3
✅ `engine/ecs/src/query.cpp` — solo iteración de queries
❌ `math_all.h`, `world.cpp` con World+Entity+Component

## Convenciones de includes

- Headers propios: `#include "infinity/<mod>/<archivo>.h"` (path desde include root)
- STL: `#include <vector>` (angle brackets)
- Orden: propio → STL → third_party, alfabético dentro de cada grupo
- Los headers públicos son self-contained: incluyen todo lo que usan.

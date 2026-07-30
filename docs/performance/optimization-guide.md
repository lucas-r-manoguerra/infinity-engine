---
type: Guide
title: "Guía de optimización"
description: "Cómo hacer el Infinity Engine más rápido — técnicas ordenadas por impacto"
status: draft
stale_after: 2026-10-01
tags:
  - performance
  - optimization
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# Guía de optimización

## Matriz de prioridades

| Esfuerzo | Impacto | Técnica |
|----------|---------|---------|
| Bajo | Medio | Precomputar pendientes de aristas por triángulo |
| Bajo | Medio | Dirty-rect tracking para XPutImage |
| Medio | Alto | Scanline span filling en vez de por píxel |
| Medio | Alto | Z-buffer con early Z discard |
| Medio | Alto | Recorrido por tiles (16×16) |
| Alto | Alto | SIMD (SSE2/AVX2) para procesamiento de píxeles |
| Alto | Muy alto | Renderizador con threads (thread separado) |
| Medio | Muy alto | Extensión XShm (blit zero-copy) |
| Bajo | Bajo | Loop unrolling manual |
| Bajo | Bajo | Sacar usleep, usar vsync o frame timing |

## 1. Precomputar pendientes de aristas (bajo esfuerzo, impacto medio)

Actualmente, cada píxel recalcula las funciones de arista desde cero. Precomputá `dx`, `dy` y el término constante para cada arista de forma que el trabajo por píxel sea una única multiplicación-suma.

**Antes** (por píxel):
```zig
const w0 = (v1.x - v2.x) * (fy - v2.y) - (v1.y - v2.y) * (fx - v2.x);
```

**Después** (por triángulo, luego por píxel):
```zig
// Precomputado
const e0_dx = v1.y - v2.y; // dy
const e0_dy = v2.x - v1.x; // -dx
const e0_c  = v1.x * v2.y - v1.y * v2.x; // cross

// Por píxel
const w0 = e0_dx * fx + e0_dy * fy + e0_c;
```

## 2. Scanline span filling (esfuerzo medio, impacto alto)

En vez de iterar cada píxel del bounding box, por cada scanline Y:
1. Computá las intersecciones X con las dos aristas que cruzan esta Y
2. Rellená el span entre min_x y max_x

Esto elimina el test de inside y los píxeles desperdiciados fuera del triángulo. ~2× de aceleración para triángulos grandes.

## 3. Recorrido por tiles (esfuerzo medio, impacto medio-alto)

Dividí la pantalla en tiles de 16×16. Por triángulo:
1. Testeá qué tiles solapa el triángulo (bounding box + test de arista por tile)
2. Para los tiles que solapan, rasterizá solo los píxeles de ese tile

Beneficio: mejor localidad de caché (las escrituras de píxeles quedan dentro de una región de 16×16 ≈ 1 KB). También facilita el threading (un tile por thread).

## 4. SIMD (SSE2/AVX2) (alto esfuerzo, impacto alto)

Procesá 4 (SSE2) u 8 (AVX2) píxeles simultáneamente:

```c
// Pseudo-SSE: 4 píxeles a la vez
__m128 fx = _mm_add_ps(base_x, _mm_set_ps(0,1,2,3));
__m128 w0 = _mm_add_ps(_mm_mul_ps(e0_dx, fx), e0_dy_fy_plus_c);
__m128 mask = _mm_and_ps(_mm_cmpge_ps(w0, zero), ...);
```

Requiere inline assembly en Zig, un archivo C separado con intrínsecos SSE, o una futura funcionalidad SIMD de Zig.

## 5. Renderizador con threads (alto esfuerzo, impacto muy alto)

Dividí el framebuffer en franjas horizontales (o tiles) y despachá cada franja a un thread:

```zig
const NUM_THREADS = 4;
const strip_height = HEIGHT / NUM_THREADS;
var threads: [NUM_THREADS]std.Thread = undefined;
for (&threads, 0..) |*t, i| {
    t.* = try std.Thread.spawn(.{}, renderStrip, .{fb, i * strip_height, strip_height, scene});
}
for (&threads) |t| t.join();
```

Beneficio: ~N× de aceleración en CPUs de N núcleos. Casi lineal para renderizado por tiles.

## 6. Extensión XShm (esfuerzo medio, impacto muy alto)

La extensión MIT-SHM de X11 permite compartir memoria entre tu proceso y el servidor X mediante `shmget`/`shmid`, eliminando la copia de XPutImage. La copia de 1.83 MB/frame desaparece.

Implementación: `XShmAttach` + `XShmPutImage` en lugar de `XPutImage`. Misma API, cero copias.

## 7. Z-buffer con early Z (esfuerzo medio, impacto alto)

Agregá un campo `z: f32` a Vertex y un depth buffer (WIDTH × HEIGHT × 4 bytes = 1.83 MB). Antes de escribir un píxel, verificá si la Z interpolada está más cerca que la Z almacenada. Si no, saltate la escritura.

Esto naturalmente descarta píxeles ocluidos — gran beneficio para escenas con profundidad compleja.

## Línea base actual (a superar)

Ejecutá el build actual con `time`:

```bash
timeout 10 zig-out/bin/infinity-engine
```

Revisá los frames renderizados al salir y dividí por 10 para obtener el fps promedio. Objetivo actual: 60 fps (~16.6 ms por frame).

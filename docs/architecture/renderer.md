---
type: Reference
title: "Software renderer — rasterización baricéntrica"
description: "Relleno de triángulos por scanline con funciones de borde e interpolación de color"
status: stable
stale_after: 2026-10-01
tags:
  - renderer
  - rasterizer
  - barycentric
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# Software Renderer

## Método de rasterización: baricéntrico (función de borde)

Cada triángulo se rasteriza píxel por píxel usando **funciones de borde** para calcular las coordenadas baricéntricas:

```
edge(A, B, P) = (B.x - A.x) × (P.y - A.y) - (B.y - A.y) × (P.x - A.x)
```

Para cada píxel `P`, se calcula `w0 = edge(V1, V2, P)`, `w1 = edge(V2, V0, P)`, `w2 = edge(V0, V1, P)`.

Un píxel está **dentro** del triángulo cuando los tres pesos tienen el mismo signo (coincidiendo con la dirección del winding).

## Interpolación baricéntrica de color

Una vez dentro, se normalizan los pesos por el área total:

```
bw0 = w0 / area
bw1 = w1 / area
bw2 = w2 / area
```

Se interpola cada canal de color:

```
r = bw0 × V0.r + bw1 × V1.r + bw2 × V2.r  (clamped a 0–255)
g = bw0 × V0.g + bw1 × V1.g + bw2 × V2.g
b = bw0 × V0.b + bw1 × V1.b + bw2 × V2.b
```

## Formato de vértice

```zig
const Vertex = struct {
    x: f32,        // screen-space X
    y: f32,        // screen-space Y
    z: f32,        // depth: 0.0 = near, 1.0 = far
    color: Color,  // per-vertex color (interpolated)
};
```

El campo `z` se interpola con los mismos pesos baricéntricos que el color y se compara contra el **Z-buffer** (depth buffer con valores iniciales = 1.0). Si el píxel está detrás de lo ya dibujado, se descarta. <sup>[ADR-005](../decisions/005-z-buffer.md)</sup>

Próximo paso: agregar `u: f32, v: f32` para texturas.

## Depth buffer

El Z-buffer tiene el mismo tamaño que el framebuffer: `WIDTH × HEIGHT × sizeof(f32)` = 1.83 MB. Se limpia cada frame con `@memset(zb, 1.0)`.

Cada píxel ejecuta:
1. Interpolar Z: `z = bw0 * v0.z + bw1 * v1.z + bw2 * v2.z`
2. Early Z-test: `if (z >= zb[zb_idx]) continue;`
3. Escribir píxel y actualizar: `zb[zb_idx] = z;`

El Z-test ocurre **antes** de la interpolación de color y la escritura del framebuffer, por lo que píxeles ocultos no desperdician ancho de banda de memoria de color.

## Pipeline por frame (actual)

```
Clear FB + Clear ZB → For each triangle:
  ├─ Model → View → Projection (MVP transform to clip space)
  ├─ Frustum clip (skip triangles with w < 0)
  ├─ Perspective divide (clip → NDC)
  ├─ Viewport transform (NDC → screen)
  ├─ Back-face cull (skip if 2D area < 0)
  ├─ Sort Y
  ├─ For each pixel in bounding box:
  │  ├─ Edge test
  │  ├─ Barycentric weights
  │  ├─ Interpolate Z + Z-test
  │  ├─ Interpolate color
  │  └─ Write pixel + update ZB
  └─ Next triangle
```

## Futuro: pipeline de fragmentos

```
Clear FB + Clear ZB → For each triangle:
  ├─ Transform vertices
  ├─ Clip / cull
  ├─ Sort Y
  ├─ For each pixel:
  │  ├─ Edge test
  │  ├─ Barycentric weights
  │  ├─ Z-test
  │  ├─ Interpolate (color | UV | normal)
  │  ├─ Sample texture
  │  ├─ Blend
  │  └─ Write pixel
  └─ Next triangle
```

## Ideas de optimización de rendimiento

| Técnica | Ganancia estimada | Complejidad |
|---------|-------------------|-------------|
| **SIMD (SSE/AVX)** — procesar 4–8 píxeles a la vez | 2–4× | Alta |
| **Scanline fill** — para cada Y, rellenar segmentos entre bordes | ~2× | Media |
| **Early-out bounding box** — saltar píxeles transparentes/a | Menor | Fácil |
| **Block-based traversal** (tiles de 16×16) | ~1.5× | Media |
| **Precompute edge slopes** — evitar recomputar por píxel | ~1.5× | Media |
| **Threaded tile rendering** | N-núcleos × | Alta |

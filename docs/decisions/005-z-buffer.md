---
type: Decision
title: "ADR-005: Z-buffer para ordenamiento por profundidad"
description: "Depth buffer que permite ordenamiento correcto de triángulos superpuestos sin importar el orden de dibujado"
status: stable
stale_after: 2026-11-01
tags:
  - z-buffer
  - depth
  - renderer
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# ADR-005: Z-buffer para ordenamiento por profundidad

**Fecha**: 2026-07-29
**Estado**: aceptada
**Decididores**: lucas + AI

## Contexto

Sin Z-buffer, el rasterizador pinta cada triángulo en el orden en que se dibuja. El último triángulo dibujado siempre se ve encima de los anteriores, incluso si debería estar detrás. Esto significa que no se puede renderizar una escena 3D correctamente sin ordenar manualmente los triángulos de atrás a adelante (algoritmo del pintor).

El algoritmo del pintor es frágil: triángulos que se intersectan, ciclos de profundidad, o mallas no convexas producen artefactos imposibles de resolver sin Z-buffer.

## Decisión

Agregar un depth buffer (Z-buffer) del mismo tamaño que el framebuffer (800 × 600 × 4 bytes = 1.83 MB) con valores `f32`.

Cada píxel almacena su profundidad Z (0.0 = cerca, 1.0 = lejos). Antes de escribir un píxel:
1. Interpolamos Z con los mismos pesos baricéntricos que usamos para color
2. Comparamos contra el valor almacenado en el depth buffer
3. Si el nuevo Z es menor (más cerca), escribimos el píxel y actualizamos el depth buffer
4. Si el nuevo Z es mayor o igual (más lejos), lo descartamos

```zig
// Misma interpolación baricéntrica que el color
const z = bw0 * v[0].z + bw1 * v[1].z + bw2 * v[2].z;

// Early Z-test
if (z >= zb[zb_idx]) continue;

// Pasa: escribir píxel y actualizar depth buffer
zb[zb_idx] = z;
```

## Alternativas

**Algoritmo del pintor (sort de atrás a adelante)**: No requiere memoria extra. Pero falla con triángulos que se intersectan (ciclos de profundidad), requiere ordenar por frame, y no escala a mallas complejas.

**W-buffer**: Usa profundidad en espacio de mundo en vez de espacio de pantalla. Más preciso para escenas con rango de profundidad extremo, pero más caro de computar y no necesario para un renderizador educativo en este momento.

**Subdivisión de triángulos**: Dividir triángulos que se intersectan. Extremadamente complejo y frágil.

## Consecuencias

### Positivas
- Triángulos se renderizan correctamente sin importar el orden de dibujado
- Habilitante para escenas 3D con profundidad arbitraria
- Misma memoria que el framebuffer (+1.83 MB en total ~3.66 MB)
- Overhead mínimo: 1 interpolación f32 + 1 comparación por píxel
- Reutiliza el mecanismo de interpolación baricéntrica existente

### Negativas
- +1.83 MB de memoria (16.6% más en sistema con 16 GB — insignificante)
- Cada frame hay que limpiar el depth buffer (1.83 MB de escritura secuencial, ~0.3 ms)
- El Z-buffer ocupa ancho de banda de caché L2

### Riesgo
La precisión `f32` puede producir Z-fighting en escenas con triángulos muy cercanos entre sí. Mitigación: usar rango de profundidad razonable (0.0–1.0) y evitar coplanaridad exacta.

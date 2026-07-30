---
type: Decision
title: "ADR-002: Renderizador por software (CPU) sobre GPU"
description: "Todo el renderizado se hace en CPU vía framebuffer — sin GPU, sin OpenGL/Vulkan"
status: stable
stale_after: 2026-10-01
tags:
  - renderer
  - cpu
  - design
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# ADR-002: Renderizador por software sobre GPU

**Fecha**: 2026-07-28
**Estado**: aceptada
**Decididores**: lucas + AI

## Contexto

Infinity Engine necesita renderizar gráficos. La opción default para cualquier motor moderno es OpenGL, Vulkan o WebGPU. Sin embargo, el objetivo del proyecto es entender en profundidad los fundamentos del renderizado — no distribuir un producto.

## Decisión

Escribir un renderizador puro por software que escriba píxeles directamente a un framebuffer. Sin APIs de GPU, sin shaders. Cada píxel se calcula en la CPU usando la misma matemática que las GPUs usan en hardware.

## Por qué

Renderizar con OpenGL/Vulkan esconde los conceptos más importantes detrás de la abstracción del driver: los framebuffers se vuelven handles `GLuint`, la rasterización es "gratis", los shaders se escriben en GLSL y los compila el driver. Un renderizador por software expone cada byte:

- Framebuffer → `[]u8` que limpiás y escribís
- Rasterización → `for (y) { for (x) { compute edge test } }`
- Interpolación → `lerp(a, b, t)` con pesos baricéntricos
- Texturing → `image[x][y]` sampleada en UVs interpolados

Cada optimización que aplicarías en un shader de GPU tiene un equivalente en CPU: SIMD, barrido por bloques, early Z, mipmapping.

## Alternativas

**MiniGL (TinyGL)**: Una implementación de OpenGL por software. Nos daría familiaridad con la API de OpenGL pero igual abstrae el rasterizador. No está alineado con el objetivo de aprendizaje.

**Modo inmediato de OpenGL**: Fácil de empezar, pero no enseña nada sobre cómo funcionan las GPUs.

**Syscall directo a DRM/KMS**: Control máximo, pero requiere cambiar de VT y ser root. Impracticable y peligroso.

## Consecuencias

### Positivas
- Control completo sobre cada píxel
- Entendimiento profundo de la matemática equivalente a GPU
- Cero dependencias de drivers (funciona en cualquier Linux con X11)
- Renderizado determinista (sin diferencias entre drivers)

### Negativas
- Más lento que GPU (especialmente con triángulos grandes / fill rates altos)
- Sin aceleración de hardware para transformaciones, iluminación, etc.
- Más código que escribir para el mismo resultado visual
- ~60 fps a 800×600 es alcanzable pero no trivial

### Riesgo
El renderizado por CPU no escala a la complejidad de juegos modernos. Está bien — esto es un motor educativo, no un producto comercial. Los techos de rendimiento son oportunidades de aprendizaje.

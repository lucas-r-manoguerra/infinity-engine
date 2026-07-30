---
type: Decision
title: "ADR-004: Sombreado Gouraud con interpolación baricéntrica"
description: "Interpolación de color por vértice usando pesos baricéntricos en lugar de sombreado plano"
status: stable
stale_after: 2026-10-01
tags:
  - shading
  - interpolation
  - renderer
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# ADR-004: Sombreado Gouraud con interpolación baricéntrica

**Fecha**: 2026-07-29
**Estado**: aceptada
**Decididores**: lucas + AI

## Contexto

El triángulo relleno inicial usaba un solo color plano. Para hacer las escenas más interesantes y avanzar hacia texture mapping, necesitamos variación de color por vértice. El mismo mecanismo de interpolación que mezcla colores de vértices también se usa para coordenadas UV (texturas), normales de vértice (sombreado Phong) y cualquier otro atributo de vértice.

## Decisión

Extender el rasterizador `fillTriangle` para que acepte tres valores `Vertex` (cada uno con su propio color) e interpolar por píxel usando pesos baricéntricos:

```zig
// Peso baricéntrico para cada vértice
bw0 = w0 / area
bw1 = w1 / area
bw2 = w2 / area

// Interpolar cada canal
r = bw0 * v0.r + bw1 * v1.r + bw2 * v2.r
```

## Por qué baricéntrico en vez de scanline

Un enfoque basado en scanline (interpolar colores a lo largo de los bordes, luego a lo ancho de los spans) es más rápido pero más complejo. La interpolación baricéntrica es:

- **La misma matemática que el edge testing** — ya calculamos `w0, w1, w2` por píxel
- **General** — funciona para CUALQUIER atributo de vértice (color, UV, normal, etc.)
- **Más simple de debuggear** — cada píxel es independiente, sin error acumulativo
- **Trivialmente correcta** — la fórmula baricéntrica es matemáticamente exacta

El costo de rendimiento de la interpolación baricéntrica contra scanline es aproximadamente 2–3× más aritmética, pero a 800×600 el fill rate sigue estando dentro del presupuesto para una escena demo.

## Alternativas

**Sombreado plano (flat shading)**: Un color por triángulo. Lo más rápido pero visualmente menos interesante. Sin base para texturas.

**Gouraud por scanline**: Interpolar colores a lo largo de los bordes mediante DDA, luego a lo ancho de los spans. ~2× más rápido que baricéntrico para triángulos grandes. Más código, más edge cases (triángulos horizontales/degenerados).

**Sombreado Phong**: Interpolación de normales por píxel + cálculo de iluminación. Más preciso visualmente pero mucho más costoso. Más adecuado para renderizado en GPU. Lo agregaremos como opción más adelante.

## Consecuencias

### Positivas
- Base para texture mapping (la interpolación de UVs usa exactamente el mismo path de código)
- Gradientes de color suaves sin subdivisión por triángulo
- Los colores de vértice pueden codificar iluminación, altura, temperatura — lo que la escena necesite
- La misma interpolación soporta todos los atributos de vértice de forma uniforme

### Negativas
- ~3× más aritmética por píxel que el sombreado plano
- Baricéntrico por píxel es más lento que la interpolación por scanline
- Bandas de color a baja precisión (mitigado con clamping a u8)

### Riesgo
El enfoque baricéntrico podría ser un bottleneck para escenas con muchos triángulos grandes. Mitigación: agregar un interpolador por scanline como fast path cuando el fill rate sea un problema, y mantener el baricéntrico como fallback para comparar correctness.

---
type: Decision
title: "ADR-003: Formato de píxeles BGRA32 para framebuffer"
description: "Formato de píxeles BGRA32 de 32 bits elegido por compatibilidad con X11 ZPixmap en x86_64"
status: stable
stale_after: 2026-10-01
tags:
  - framebuffer
  - format
  - x11
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# ADR-003: Formato de píxeles BGRA32 para framebuffer

**Fecha**: 2026-07-28
**Estado**: aceptada
**Decididores**: lucas + AI

## Contexto

XPutImage espera datos de píxeles en un formato específico determinado por el visual default del display X11. En un sistema x86_64 Linux típico con profundidad de 24 bits, el formato `ZPixmap` de X11 espera píxeles en orden little-endian donde el azul ocupa el byte menos significativo.

## Decisión

Usar un formato de píxeles BGRA32 empaquetado:

```zig
const Color = packed struct(u32) {
    b: u8,     // byte 0 (LSB)
    g: u8,     // byte 1
    r: u8,     // byte 2
    a: u8,     // byte 3 (MSB)
};
```

- `buffer[0..3]` = B, G, R, A
- Como entero LE de 32 bits: `0xAARRGGBB`

## ¿Por qué no RGBA?

En x86 little-endian, `RGBA` en memoria se interpretaría como `0xABGR` al leerlo como u32, lo que significa que XPutImage intercambiaría los canales rojo y azul. Podríamos arreglarlo con una conversión de formato, pero eso desperdicia ciclos de CPU por píxel. Usar el orden de bytes nativo del display evita la conversión por completo.

## Alternativas

**RGBA + conversión de formato al blitear**: Mantiene un orden de píxeles más intuitivo en código, pero cada píxel se procesa dos veces (una para renderizar, otra para convertir). ~1.83 MB de escrituras extra por frame — overhead medible a 60 fps.

**RGB565 (16 bits)**: La mitad del ancho de banda de memoria. Pero pierde precisión de color (64K colores vs 16M), y XCreateImage requiere manejar `bitmap_pad` distinto. No vale la complejidad en esta etapa.

**ARGB32**: Común en algunos sistemas, pero la confusión del orden de bytes es más difícil de razonar (¿alpha es el primer byte o el último?). BGRA no es ambiguo en LE x86.

## Consecuencias

### Positivas
- Camino zero-copy del render a la pantalla — X11 lee el buffer tal cual está
- Modelo de memoria simple: `fb[i+0..3]` mapea directamente a los bytes del píxel
- Fácil de razonar: cada byte es un canal

### Negativas
- BGRA es poco intuitivo para desarrolladores acostumbrados a RGBA (ej: `Color{ .r=255, .g=0, .b=0 }` renderiza rojo, pero inicializás `.b` primero en el struct)
- No es portable a sistemas big-endian (no es una preocupación para x86_64 Linux)
- XShm (optimización futura) necesitaría el mismo formato

### Riesgo
Si portamos a Wayland o un backend que no sea X11, el orden de bytes puede cambiar. El formato del framebuffer debería volverse una constante configurable cuando eso ocurra, no estar hardcodeado.

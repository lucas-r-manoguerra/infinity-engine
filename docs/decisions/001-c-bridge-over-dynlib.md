---
type: Decision
title: "ADR-001: Bridge en C sobre Zig DynLib"
description: "Todas las llamadas X11 pasan por wrappers en C para evitar el bug de DynLib en Zig 0.16.0"
status: stable
stale_after: 2026-10-01
tags:
  - x11
  - bridge
  - zig
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# ADR-001: Bridge en C sobre Zig DynLib

**Fecha**: 2026-07-28
**Estado**: aceptada
**Decididores**: lucas + AI

## Contexto

Necesitamos llamar funciones de X11 desde Zig sin linkear contra X11 en tiempo de compilación (para no requerir los headers de desarrollo de X11). Zig provee `std.DynLib` para cargar bibliotecas compartidas en tiempo de ejecución y resolver símbolos. Sin embargo, Zig 0.16.0 emite una instrucción `call *%rax` rota al llamar punteros a función obtenidos con `DynLib.lookup()` — `%rax` siempre es 0x0, causando un segfault.

## Decisión

Escribir un bridge liviano en C (`x11_bridge.c`) que use `dlopen`/`dlsym` directamente, compilarlo como un `.o` estático, y llamar las funciones wrapper desde Zig mediante `extern fn` con `callconv(.c)`.

## Alternativas

**Zig DynLib**: Enfoque nativo limpio, pero crashea en runtime con 0.16.0. Es viable cuando el bug de Zig se corrija en una versión posterior.

**Linkeo estático -lX11**: El enfoque más simple, pero requiere los headers de desarrollo de X11 al compilar. Aceptable para desarrollo pero agrega fricción.

**Syscall directo (write a /dev/dri o /dev/fb)**: Control máximo, pero implica reimplementar el protocolo X11. Scope prohibitivo.

## Consecuencias

### Positivas
- No se necesitan headers de X11 al compilar
- El mismo patrón funciona para cualquier biblioteca `.so` (SDL, OpenGL, etc.)
- Cadena de llamadas estable y predecible

### Negativas
- 3 puntos de contacto por función X11 nueva (typedef, dlsym, wrapper)
- Ligeramente opaco — el lado Zig solo ve funciones extern
- Se requiere compilador C + `-ldl` en el build

### Riesgo
- Si Zig corrige DynLib, esto se vuelve boilerplate innecesario. Riesgo bajo: el bridge tiene ~250 líneas y es trivial de mantener.

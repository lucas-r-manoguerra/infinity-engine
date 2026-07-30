---
type: Reference
title: "Build system — integración Zig + C"
description: "Cómo build.zig compila el C bridge y enlaza el ejecutable"
status: stable
stale_after: 2026-10-01
tags:
  - build
  - zig
  - c
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# Build system

## Estructura

```zig
// build.zig
const libc_mod = b.createModule(.{
    .link_libc = true,
    .target = target,
    .optimize = optimize,
});
libc_mod.addCSourceFile(.{ .file = b.path("src/x11_bridge.c"), .flags = &.{} });
```

Detalles clave:
- `link_libc` va DENTRO de `b.createModule(...)` — NO como una llamada separada `.link_libc = true`
- `addCSourceFile` se invoca sobre el **Module**, no sobre `Step.Compile`
- El módulo compila `x11_bridge.c` a un `.o` estático que se enlaza al ejecutable
- `ldl` se enlaza aparte para `dlopen`/`dlsym`/`dlclose`

## Cadena de dependencias completa

```
x11_bridge.c  ──compile──▶ x11_bridge.o ──┐
                                          ├──link──▶ infinity-engine
main.zig  ────compile────────────────────▶┘
                                            └── -ldl
```

## Por qué no hay dependencia de X11 en tiempo de compilación

X11 se carga en **tiempo de ejecución** vía `dlopen("libX11.so.6")`. No hay `-lX11` en tiempo de enlace. Esto significa:
- No se necesitan headers de desarrollo de X11 (no hace falta `apt install` para compilar)
- El binario funciona en cualquier sistema con `libX11.so.6` instalado (la mayoría de los Linux de escritorio)
- Falla de forma controlada (o trapea) si X11 no está disponible

## Agregar una nueva función en C

Tres puntos de contacto:
1. Agregar el tipo de puntero a función en `x11_bridge.c` (ej.: `typedef int (*store_name_t)(Display*, XID, const char*);`)
2. Agregar campo + dlsym en `x11_bridge_init()`
3. Agregar wrapper function + `extern fn` en `main.zig`

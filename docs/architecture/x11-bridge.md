---
type: Reference
title: "C Bridge X11 — patrón dlopen/dlsym"
description: "Cómo y por qué las llamadas X11 se enrutan vía C en lugar de DynLib de Zig"
status: stable
stale_after: 2026-10-01
tags:
  - x11
  - bridge
  - dlopen
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# C Bridge X11

## Problema

Zig 0.16.0 tiene un bug en DynLib donde invocar un puntero a función cargado vía `dlsym` crashea en la dirección 0x0. El compilador emite `call *%rax` pero `%rax` es cero — el puntero nunca se materializó. Esto vuelve `DynLib.lookup()` inusable para llamar funciones de X11 en tiempo de ejecución.

## Solución

Enrutar TODAS las llamadas X11 vía código en C usando `dlopen`/`dlsym` en un archivo `.c` compilado como objeto estático. Zig llama wrappers livianos en C mediante `extern fn`:

```
Zig → extern fn → C wrapper → dlsym'd function pointer → libX11.so.6
```

## Arquitectura

```
┌─────────────────────────────────────────────────────┐
│  src/main.zig                                       │
│  extern fn x11_open_display(b: *X11Bridge, ...) ...  │
│  extern fn x11_put_image(b: *X11Bridge, ...) ...     │
└────────────────┬────────────────────────────────────┘
                 │ callconv(.c)
┌────────────────▼────────────────────────────────────┐
│  src/x11_bridge.c                                    │
│  X11Bridge { void* handle; fnptr* XOpenDisplay; ... }│
│  x11_bridge_init() → dlopen → dlsym all function ptrs│
│  x11_open_display() → (*b->XOpenDisplay)(...)        │
│  x11_put_image() → (*b->XPutImage)(...)              │
└────────────────┬────────────────────────────────────┘
                 │ runtime dynamic loading
┌────────────────▼────────────────────────────────────┐
│  libX11.so.6 (no -dev headers needed at build time) │
└─────────────────────────────────────────────────────┘
```

## Struct de estado del bridge

```c
typedef struct {
    void* handle;                    // dlopen handle
    open_display_t       XOpenDisplay;
    default_visual_t     XDefaultVisual;
    create_image_t       XCreateImage;
    put_image_t          XPutImage;
    store_name_t         XStoreName;
    // ... 17 function pointers total
} X11Bridge;
```

## Por qué funciona

Las llamadas a funciones en C mediante una cadena puntero-a-puntero compilan a código de máquina estable en x86_64. El struct `X11Bridge` contiene todos los punteros a función, poblados una sola vez en init. Cada llamada es una doble indirección (`b->XOpenDisplay`), que es predecible y no dispara el bug de DynLLVM de Zig.

## Compensaciones

| Pro | Contra |
|-----|--------|
| Sin headers de X11 en tiempo de compilación | Overhead de doble llamada a función (trivial) |
| Soluciona el bug de Zig 0.16.0 | Declaraciones manuales de tipos de puntero a función |
| Carga en tiempo de ejecución sin dependencias del linker | Cada función nueva de X11 requiere 3 puntos de contacto |
| El mismo patrón funciona para cualquier .so | — |

## Wrappers actuales (17 funciones)

XOpenDisplay, XDefaultVisual, XDefaultScreen, XRootWindow, XBlackPixel, XWhitePixel, XCreateSimpleWindow, XMapWindow, XCloseDisplay, XFlush, XDefaultDepth, XCreateGC, XFreeGC, XNextEvent, XPending, XInternAtom, XSetForeground, XDrawLine, XClearWindow, XChangeProperty, XCreateImage, XPutImage, XDestroyImage, XStoreName

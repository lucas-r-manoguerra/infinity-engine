---
type: Reference
title: "Arquitectura del framebuffer"
description: "Layout del buffer de píxeles, wrapping de XImage y pipeline render→present"
status: stable
stale_after: 2026-10-01
tags:
  - framebuffer
  - pipeline
  - rendering
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# Framebuffer

## Modelo de memoria

Buffer lineal único de bytes, alocado por `page_allocator` de Zig:

```
WIDTH=800, HEIGHT=600, BYTES_PER_PIXEL=4
FB_SIZE = 800 × 600 × 4 = 1,920,000 bytes ≈ 1.83 MB
```

## Formato de píxel: BGRA32

```zig
const Color = packed struct(u32) {
    b: u8,     // Blue  — byte offset +0
    g: u8,     // Green — byte offset +1
    r: u8,     // Red   — byte offset +2
    a: u8,     // Alpha — byte offset +3
};
```

¿Por qué BGRA y no RGBA? El formato `ZPixmap` de X11 en x86_64 espera el orden nativo little-endian: azul en el byte menos significativo, alfa en el más significativo. En little-endian x86, BGRA en memoria mapea directamente a `0xAARRGGBB` cuando se lee como entero de 32 bits.

## Wrapping de XImage

XPutImage espera un `XImage*` apuntando a los datos de píxel. Creamos uno que apunte DIRECTAMENTE al buffer alocado por Zig — cero copias:

```zig
const ximage = x11_create_image(bridge, dpy, visual,
    @intCast(depth), ZPixmap, 0, fb.ptr, WIDTH, HEIGHT, 32, 0);
```

- `data = fb.ptr` — XImage usa nuestra memoria directamente
- `bitmap_pad = 32` — cada píxel está alineado a 32 bits
- `bytes_per_line = 0` — X11 lo calcula como `WIDTH * 4`

## Pipeline del frame

```
1. clearFramebuffer(fb, dark_bg)     → fill estilo memset
2. fillTriangle(fb, v0, v1, v2)      → escribe datos de píxel en fb
3. windowPresent() → XPutImage        → blit fb a la ventana X11
4. windowPollEvents()                 → drena la cola de eventos X11
5. usleep(16_000)                     → limita a ~60 fps
```

## Características de rendimiento

| Operación | Costo | Notas |
|-----------|-------|-------|
| `clearFramebuffer` | ~0.3 ms | Escritura lineal de 1.83 MB |
| `fillTriangle` (promedio) | ~0.5–2 ms | Depende del área del triángulo |
| `XPutImage` | ~1–3 ms | Copia 1.83 MB a memoria compartida de X |
| Frame total | ~16.6 ms | ≈60 fps con usleep |

## ¿Por qué no doble buffer?

Con un solo buffer alcanza para un software renderer en esta etapa. XPutImage hace una copia implícita a la memoria compartida de X, así que no hay riesgo de tearing. Habría que agregar doble buffer cuando el renderer sea lo suficientemente rápido como para que la copia de XPutImage sea el cuello de botella.

## Próximas optimizaciones

- Dirty rect tracking: blitear solo las regiones modificadas
- Threaded renderer: escribir píxeles en un hilo separado mientras X11 maneja eventos
- Extensión XShm de memoria compartida: eliminar la copia de XPutImage

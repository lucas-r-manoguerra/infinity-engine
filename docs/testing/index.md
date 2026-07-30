---
type: Concept
title: "Testing del Infinity Engine"
description: "Estrategia de test, objetivos de cobertura y cómo verificar correctness"
status: draft
stale_after: 2026-10-01
tags:
  - testing
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# Testing

## Estado actual

No existen tests automatizados todavía. El proyecto está en prototipado temprano (Fases 1–4). La verificación es manual:
- **Visual**: ejecutá el engine, observá que el renderizado sea correcto (3 capas de profundidad)
- **Exit codes**: el programa termina limpio (sin crash, sin leaks)
- **Build**: `zig build` funciona sin errores

## Por qué todavía no hay tests

En esta etapa, el código de renderizado cambia cada sesión — rasterizers, formatos de vértices y pipelines están en pleno cambio. Escribir tests para código que se va a reescribir mañana es tiempo perdido. Estamos usando **prototipado exploratorio**: construí, ejecutá, fijate si se ve bien, iterá.

Esto cambia una vez que la API se estabilice.

## Estrategia de tests (futura)

### 1. Golden frame tests (máximo valor)

El renderer es determinístico — dados los mismos vértices y tiempo, produce exactamente el mismo framebuffer. Guardá un golden `.ppm` o `.png` de referencia y compará:

```zig
test "rotating triangle matches golden frame" {
    var fb: [WIDTH * HEIGHT * 4]u8 = undefined;
    renderScene(&fb, time = 0.0);
    try testing.expectEqualSlices(u8, &golden_frame_0, &fb);
}
```

**Por qué funciona**: el software renderer es una función pura de (vértices, tiempo) → píxeles. Sin varianza de driver GPU, sin muestreo aleatorio, sin no-determinismo de punto flotante.

**Precaución**: los golden frame tests se rompen ante cualquier cambio en el renderer. Actualizá los golden files intencionalmente.

### 2. Unit tests para funciones matemáticas

El core matemático es puro y fácil de testear:

| Función | Qué testear |
|----------|-------------|
| `edge(A, B, P)` | Inside, outside, on-boundary |
| `barycentric weights` | El centroide del triángulo devuelve pesos iguales |
| `color interpolation` | Un triángulo de color sólido devuelve el color correcto |
| `bounding box` | Bounds correctos para triángulos rotados |

### 3. Regression tests para casos borde

| Caso | Qué testea |
|------|---------------|
| Triángulo degenerado (colineal) | Sin división por cero, sin píxeles dibujados |
| Triángulo fuera de pantalla | Clipping a los límites del viewport |
| Triángulo pinche (muy fino) | Sin artefactos por geometría sub-pixel |
| Triángulo de un píxel | Triángulo válido mínimo |
| Z-buffer | Triángulos en orden inverso se ven igual que en orden correcto |
| Z-test | Píxeles detrás del depth buffer no se escriben |
| Alpha blending | Compositing correcto |

### 4. Performance regression tests

Monitoreá el frame time como métrica de CI:
```zig
test "frame time budget" {
    var timer = std.time.Timer.start()?;
    renderScene(&fb, 0.0);
    const elapsed = timer.lap();
    try testing.expect(elapsed < 20_000_000); // <20ms
}
```

## Objetivos de cobertura

| Fase | Objetivo | Qué |
|-------|--------|------|
| Actual | 0% | Prototipado |
| API freeze | >60% | Unit tests de math + rasterizer |
| Renderer estable | >80% | Golden frames + rendimiento |
| Primer release | >85% | Todos los casos borde cubiertos |

## Herramientas

- `zig test` — test runner nativo
- `zig build test` — desde build.zig
- Snapshot PPM manual: guardá el `fb` crudo como [PPM binario](https://netpbm.sourceforge.net/doc/ppm.html) para diffing

## Cómo ejecutar verificación manual

```bash
zig build && zig build run
```

Verificá:
- La ventana se abre en el tamaño correcto (800×600)
- El triángulo se renderiza con colores correctos (gradiente Gouraud)
- Hay 3 capas de profundidad: fondo gris atrás, triángulo Gouraud en el medio, diamante al frente
- El diamante frontal se ve **completo** — ni el fondo gris ni el triángulo grande lo atraviesan
- La rotación es suave (~60 fps)
- ESC cierra la ventana limpiamente
- Sin artefactos (píxeles fuera del triángulo, colores incorrectos, parpadeo)

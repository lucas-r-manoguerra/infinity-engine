# Rendimiento

| Área | Línea base | Objetivo | Notas |
|------|------------|----------|-------|
| Tiempo por frame | ~16.6 ms | <16 ms (60+ fps) | Actual: usleep(16000) + render |
| Limpiar FB + ZB | ~0.4 ms | <0.1 ms | SIMD memset podría ayudar |
| Transform 3D (12 triáng) | <0.01 ms | — | 576 ops fp, negligible |
| Relleno de triángulos (prom) | ~1 ms | <0.2 ms | Precomputar pendientes de aristas |
| XPutImage blit | ~1–3 ms | <1 ms | Extensión XShm |
| Back-face culling | ~0.001 ms | — | ~50% menos triángulos rasterizados |
| Uso de CPU | 1 núcleo | — | Sin threading todavía |

## Cómo medir

El perfilado actual es manual mediante `time` / strace. Un contador de frames imprime el total al salir. Para mayor granularidad, insertá `std.time.microTimestamp()` alrededor de las secciones críticas.

## Cuellos de botella (actuales)

1. **Barycentric por píxel** — cada píxel calcula 3 funciones de arista + 3 interpolaciones + Z-test
2. **Copia XPutImage** — copia implícita de 1.83 MB a través de memoria compartida de X11
3. **Sin culling** — se procesa cada píxel de cada triángulo, incluso si está ocluido
4. **Z-buffer clear** — 1.83 MB de memset por frame (~0.2 ms)
5. **Overhead de Z-test** — 1 interpolación f32 + 1 comparación por píxel adicional (insignificante contra los 3 edge computes)

## Guías

- [Guía de optimización](optimization-guide.md) — técnicas concretas ordenadas por impacto

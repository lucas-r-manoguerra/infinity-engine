---
type: Decision
title: "ADR-006: Pipeline 3D con transforms y proyección"
description: "Sustitución de escena 2D por pipeline completo modelo → vista → proyección → viewport con back-face culling"
status: stable
stale_after: 2026-11-01
tags:
  - 3d
  - transform
  - pipeline
  - camera
generated:
  by: gentle-ai/1.0
  at: "2026-07-29"
---

# ADR-006: Pipeline 3D con transforms y proyección

**Fecha**: 2026-07-29
**Estado**: aceptada

## Contexto

Hasta ahora, `renderScene()` usaba `cos()` y `sin()` para posicionar triángulos en 2D. No existía el concepto de coordenadas del mundo, cámara, o proyección. Cada nuevo objeto requería calcular manualmente su posición en píxeles.

Para un engine 3D real, necesitamos un pipeline que transforme vértices 3D a través de una cámara virtual y los proyecte en pantalla.

## Decisión

Implementar el pipeline clásico de gráficos 3D:

```
Vértices del modelo → Model Matrix → World Space
                     → View Matrix → View Space (cámara)
                     → Projection Matrix → Clip Space
                     → Perspective Divide → NDC [-1, 1]
                     → Viewport Transform → Screen Space
                     → Back-face Culling
                     → Rasterizer (Z-buffer)
```

### Componentes implementados

**Model**: `Mat4.rotateY(time) * Mat4.rotateX(time * 0.3)` — rotación doble para efecto visual dinámico.

**View**: `Mat4.lookAt(eye, center, up)` — cámara ubicada en (0, 0, -5), mirando al origen.

**Projection**: `Mat4.perspective(fov, aspect, near, far)` — FOV 60°, aspect 4:3, near 0.1, far 100.

**Back-face culling**: Se calcula el área 2D con signo de cada triángulo después de proyectar a screen space. Si `area < 0`, el triángulo es back-facing y se descarta. Esto reduce los triángulos rasterizados a la mitad (~6 en vez de 12 para un cubo).

**Frustum clipping**: Triángulos con cualquier vértice `w < 0` se descartan (detrás de la cámara).

### Demo: cubo Gouraud

Se define un cubo de ±1.5 unidades con 8 vértices y 12 triángulos (2 por cara). Cada cara tiene un esquema de color distinto con variación Gouraud entre vértices:

| Cara | Color base |
|------|-----------|
| Frontal (+Z) | Rojo |
| Trasera (-Z) | Azul |
| Derecha (+X) | Verde |
| Izquierda (-X) | Amarillo |
| Superior (+Y) | Blanco |
| Inferior (-Y) | Gris |

## Consecuencias

### Positivas
- Pipeline 3D completo y funcional, habilitante para cualquier modelo 3D
- Back-face culling: ~50% menos triángulos rasterizados (rendimiento "gratis")
- Reutiliza el rasterizador baricéntrico existente sin cambios
- La escena demo es un cubo 3D real, no triángulos 2D con cos/sin
- El mismo pipeline soporta cualquier mesh que se le pase

### Pendientes
- Clipping contra los planos near/far/l/r/t/b real (no solo w < 0)
- Perspective-correct interpolation para texturas UV
- Transformaciones anidadas (scene graph con jerarquía padre-hijo)
- Mat4.inverse agregada para futura matriz de normales

### Rendimiento
El pipeline agrega overhead de transformación por vértice: 12 triángulos × 3 vértices × 16 mul-add = 576 operaciones de punto flotante por frame — insignificante contra la rasterización.

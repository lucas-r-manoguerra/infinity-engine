# Regla 07 — Convenciones Matemáticas

> El contrato matemático del engine. No se negocia: todo el código y todos los módulos
> lo comparten.

## Sistema de coordenadas

- **Right-handed**, **+Y up**, **-Z forward** (estándar OpenGL/Vulkan).
- Ángulos: **grados en la API pública**, **radianes internos**. Conversión explícita en los bordes.

## Matrices

- **Column-major**, filas/columnas según la convención de la API gráfica:
  - Almacenamiento plano `float[16]`: `column-major` (m[col*4 + row]).
  - Multiplicación: `result = a * b` significa "aplica b y después a" (transforma columnas).
- **Mat4 transforma vectores como columnas**: `v' = M * v`.
- **SRT order**: `M = T * R * S` (escala, luego rotación, luego traslación).
- Cámara y proyección siguen la convención Vulkan/GL (clip space, depth 0..1).

## Quaternions

- Orden interno: `[x, y, z, w]`, `w` es la parte escalar.
- Rotación estándar: **Yaw (Y), Pitch (X), Roll (Z)** — orden YXZ al componer desde euler.
- **Slerp** para interpolaciones de rotación; normalize antes y después.

## Transform

- Un transform es `(position: Vec3, rotation: Quat, scale: Vec3)`.
- La matriz se deriva como `T * R * S` (regla SRT).
- Transform padres: composición `child * parent` aplica child local y después parent.

## Reglas duras

1. Nunca inventar una convención local — si algo no encaja, es un bug de diseño.
2. Las funciones de math son `const`/puras: no mutan sus entradas.
3. Edge cases testeados explícitamente: división por cero, vectores nulos, ángulos límite,
   quats de 180°, NaN/Inf.
4. **Math determinista** (ADR-056): sin `-ffast-math`; política NaN/Inf explícita; el mismo
   código produce el mismo resultado en todas las plataformas (ver regla 11).

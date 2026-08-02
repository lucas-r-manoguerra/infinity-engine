# Regla 08 — Performance

> El frame budget del engine es el contrato de rendimiento. Los hot paths se miden,
> no se adivinan.

## Frame budget

| Target | Presupuesto | Notas |
|---|---|---|
| 60 FPS | 16.66 ms/frame | mínimo aceptable para el MVP |
| 120 FPS | 8.33 ms/frame | objetivo de calidad |
| Hot path ECS query | < 1 ms | 10k entidades |
| Allocation en hot path | 0 | después de init |

## Métricas y baselines

Referencia completa en `docs/ROADMAP.md` §7 (targets y mediciones C++).

- `mat4.mul`: ~34 ns | `mat4.inverse`: ~18 ns | `quat.slerp`: ~75 ns
- `entity.create`: ~8 ns | query 10k: ~170 μs | targets nuevos: `query empty` < 5 μs, `arena alloc` < 60 ns
- Renderer (medido F4.9): `renderer.triangle` ~11.1 μs | `renderer.pixel` ~20.4 ns | `renderer.full_frame` ~0.36 ms

## Reglas duras

1. **Medir antes de optimizar**: un hot path se perfila (perf, callgrind) antes de tocar nada.
2. **0 allocaciones en el frame** después del init: buffers upfront, arenas frame-scoped (regla 03).
3. **Data-oriented**: los sistemas iteran arrays planos de componentes; nada de recorrer
   el mundo por punteros dispersos. La jerarquía ECS será de archetypes (ADR-007, F13);
   el MVP actual usa sparse sets con la misma interfaz pública.
4. **SIMD como meta, no como ley**: primero versión clara y correcta; se vectoriza
   (SSE/AVX2) solo donde el profiler diga que importa, con fallback portable.
5. **El renderer software** rasteriza por tiles con backface culling activo desde el día 1
   (ADR-004).
6. **Benchmarks en release**: los números se toman con `infinity-bench`, en release,
   y se comparan contra los baselines del ROADMAP.
7. Una optimización que rompe legibilidad sin ganancia medible se rechaza en review.
8. Un cambio que degrada una métrica clave >10% sin justificación se rechaza en review.

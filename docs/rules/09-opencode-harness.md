# Regla 09 — Harness de opencode

> Cómo trabaja el agente en este repo. Esta regla define el flujo de verificación
> que opencode debe seguir en CADA tarea, para que "listo" siempre signifique lo mismo.

## Flujo obligatorio antes de declarar done

1. **Leer las reglas**: INDEX + reglas relevantes al módulo tocado.
2. **Entender el contexto**: buscar en Engram (`mem_search`) si hay memoria previa
   del módulo/feature; si el usuario menciona trabajo pasado, siempre.
3. **Escribir el test primero** (rojo) — ver regla 06.
4. **Implementar** siguiendo 02 (estilo), 03 (memoria), 04 (errores), 07 (math si aplica).
5. **Formatear**: `./scripts/format.sh` (clang-format + clang-tidy).
6. **Build**: `cmake --build --preset debug`.
7. **Tests**: `ctest --preset debug` — TODO verde y 0 leaks (ASan).
8. **Bench si toca hot path**: `infinity-bench` en release contra baselines (regla 08).
9. **Guardar memoria**: `mem_save` con decisiones, bugs, discoveries (Engram).
10. **Reportar**: resumen con archivos tocados, tests corridos, métricas.

## Definición de "listo"

- `ctest --preset debug` verde completo (incluye sanitizers).
- `./scripts/format.sh` sin diffs pendientes.
- Tests escritos ANTES que la implementación (verificable en la historia de commits).
- Sin dependencias nuevas sin auditar (ADR-068): vendored tras interfaz propia (ADR-061),
  con licencia + provenance + why.
- Documentación actualizada si cambió comportamiento público (API, config, decisiones).
- Memoria de Engram guardada con lo aprendido.

## Reglas del agente

1. **Nunca escribir código de producción sin test.** Punto.
2. **Nunca correr el build manualmente** fuera de CMake presets (regla 05).
3. **Nunca agregar dependencias** sin pasar por la regla 05: vendored tras interfaz propia
   (ADR-061) + licencia/provenance/why auditados (ADR-068).
4. **Nunca tocar código de un módulo sin leer sus reglas** (math → 07, etc.).
5. **Preguntar en vez de adivinar**: si una decisión de producto/diseño es ambigua,
   preguntar al usuario con opciones concretas — no inventar.
6. **Responder en el idioma del usuario** (español rioplatense, natural), pero código,
   comentarios, docs técnicas y nombres en inglés (regla 10 de INDEX).
7. **Verificar antes de afirmar**: no dar por hecho lo que dice la memoria — comprobar
   contra el código actual si hay duda.
8. **No editar `.clang-format`/`.clang-tidy`/`.github/` sin pedir permiso** — son contrato.

## Flujo de review (cuando aplique)

- Cambios > 300 líneas: proponer división en PRs chained (skill `chained-pr`).
- Cambios en hot paths: pedir benchmarks antes/después en el PR.
- Cambios de arquitectura (nuevo módulo, nueva dependencia, cambio de API pública):
  escribir ADR y pedir aprobación explícita.

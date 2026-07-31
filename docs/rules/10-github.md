# Regla 10 — GitHub y Commits

> Flujo de entrega: conventional commits, PRs revisables, CI verde obligatorio.

## Commits

- **Conventional Commits**: `feat:`, `fix:`, `refactor:`, `test:`, `docs:`, `build:`, `perf:`, `chore:`.
- Scope cuando aporta: `feat(math): add quat slerp`, `fix(ecs): query iteration bounds`.
- Mensaje en **inglés**, cuerpo cuando explica el porqué (no el qué).
- Sin atribución AI en commits. Sin commits vacíos.

## PRs

- **PR chico = PR revisable**: objetivo < 400 líneas de diff. Si excede, chained PRs (skill `chained-pr`).
- Cada PR incluye: tests, docs si cambia API pública, benchmarks si toca hot paths.
- Chequeo pre-PR (local, obligatorio):
  1. `./scripts/format.sh` sin diffs.
  2. `ctest --preset debug` verde.
  3. `cmake --build --preset ci` verde (tidy + format check).
  4. Bench si corresponde (release).
  5. Licencias auditadas si entró dependencia nueva (ADR-068).
  6. Docs generadas sincronizadas (ADR-026) y API pública documentada.
- La rama base es `main`; commits rebaseados limpios antes del merge.

## CI (GitHub Actions)

- Jobs: build+test (preset `ci`), clang-format check, clang-tidy, benchmarks opcional.
- CI verde es condición para merge. Un CI rojo se arregla o se revierte — nunca se mergea.
- En el PR se adjunta el resultado de benchmarks si el cambio toca métricas del ROADMAP.

## Reglas duras

1. Nunca pushear directo a `main` (salvo emergencias acordadas).
2. Nunca commitear secrets, binaries, `build/`, `.cache/`.
3. `git add` explícito de archivos intencionales; revisar `git diff` antes de commitear.
4. Un cambio que degrada una métrica >10% sin justificación no se mergea (regla 08).
5. La historia cuenta: tests primero (rojo → verde) es verificable en los commits.

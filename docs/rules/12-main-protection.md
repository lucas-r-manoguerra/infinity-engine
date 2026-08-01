# Regla 12 — Protección de main (HARD RULE)

> `main` / `origin main` es un contrato de entrega, no un workspace. Esta regla es
> una HARD RULE: no tiene excepciones locales.

## Qué se puede hacer en main / origin main

- **Solo una cosa**: mergear PRs desde ramas secundarias (`gh pr merge <n> --merge`).

## Qué está PROHIBIDO en main / origin main

1. **Borrar**: nada. No se borran archivos, ramas, tags ni remotes desde main.
2. **Crear**: nada. No se crean archivos, ramas, tags ni commits directamente en main.
3. **Modificar**: nada. No se editan archivos, no se commitea, no se pushea, no se hace
   rebase ni reset de main.

## Cómo se trabaja

- Todo el trabajo vive en **ramas secundarias** (`feat/...`, `fix/...`, `chore/...`)
  creadas desde `origin/main` tras `git fetch` — nunca desde un main local
  desactualizado.
- Los cambios llegan a main **únicamente** mediante PRs mergeados con
  `gh pr merge <n> --merge` (merge, no squash ni rebase, salvo decisión explícita
  del maintainer).
- El estado de main se consulta con `git log origin/main` / `gh pr view` — nunca
  trabajando sobre la rama.
- La limpieza de ramas feature ya mergeadas (borrar ramas locales/remotas) **no toca
  main** y está permitida.
- Un commit suelto en main, un push directo o una modificación local de main =
  violación de esta regla; se revierte.

## Verificación

- El agente nunca usa `git checkout main` para trabajar: solo para leer o para
  mergear PRs.
- Antes de cualquier operación git, preguntarse: "¿esto modifica main?". Si la
  respuesta es sí y no es un `gh pr merge`, está prohibido.

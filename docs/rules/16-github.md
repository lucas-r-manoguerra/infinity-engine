# GitHub Operations Rules

## CLI First — `gh` siempre
Siempre que haya una operación de GitHub (PR, issue, release, check status), usá `gh` — es más rápido, más preciso, y deja trace en el historial.

```bash
# ✅ Hacer
gh pr create --title "feat: ..." --body "Closes #123"
gh issue view 42 --json title,labels,state
gh release create v0.2.0 --notes-from-tag

# ❌ No hacer — navegar a GitHub manual para cosas que gh resuelve en segundos
```

---

## Issues

### Ciclo de Vida
```
🔴 bug       → triage → asignado → fix branch → PR → close
🟡 feature   → triage → milestone → spec → impl → PR → close
⚪ discussion → triage → decision → convert a issue o close
```

### Todo issue necesita
- **Título claro**: `[area] verbo corto` → `[ecs] entity handle generation leak`, `[renderer] add Vulkan pipeline cache`
- **Labels**: mínimo `type:bug|feature|chore` + `priority:critical|high|medium|low` + milestone
- **Cuerpo**: qué, por qué, cómo reproducir (bugs), contexto (features)

### Labels obligatorios
| Label | Descripción |
|---|---|
| `type:bug` | Comportamiento incorrecto |
| `type:feature` | Funcionalidad nueva |
| `type:chore` | Refactor, deuda técnica, tooling |
| `type:discussion` | Decisión abierta |
| `priority:critical` | Bloquea release, atención inmediata |
| `priority:high` | Debería estar en el próximo milestone |
| `priority:medium` | Importante pero no urgente |
| `priority:low` | Nice to have |
| `milestone:M1` (M2, M3...) | Asociado a un milestone |
| `status:blocked` | Depende de otro issue/PR |

```bash
# Crear issue rápido
gh issue create \
  --title "[ecs] query iteration O(n) en archetype vacío" \
  --label "type:bug,priority:high,milestone:M1" \
  --body "**Describe**: ...\n**Reproducir**: ..."
```

---

## Pull Requests

### Formato
- **Título**: `type(scope): descripción corta` — conventional commits
  - ✅ `feat(ecs): add query filtering by component mask`
  - ✅ `fix(renderer): BGRA32 pixel offset on resize`
  - ✅ `docs(rules): add GitHub operations guide`
  - ❌ `fix bug`, `update stuff`, `wip`
- **Cuerpo**: describe QUÉ y POR QUÉ, no CÓMO (el diff ya muestra el cómo)
- **Referencia**: el issue que resuelve → `Closes #123` o `Related to #456`

### Review Gates (HARD)
1. `zig build test` pasa (0 failures, 0 leaks)
2. `zig build` compila sin warnings
3. El diff no excede ~400 líneas (si excede, partir en PRs encadenados)
4. Código nuevo tiene tests
5. No viola reglas de `docs/rules/`

### Merge Strategy
| Situación | Estrategia | Commits en main |
|---|---|---|
| Feature branch | Squash merge | 1 commit limpio |
| Bug fix | Squash merge | 1 commit |
| Chained PRs (stacked) | Rebase merge | Cada PR mantiene su commit |
| Release branch | Merge commit | Visible fork point |

### Chained PRs (stacked diffs)
Cuando un cambio es grande (>400 líneas) y se puede dividir lógicamente:
1. Crear PR#1 contra `main` con la primera parte
2. Crear PR#2 contra la branch de PR#1 con la siguiente parte
3. Mergear en orden: PR#1 → PR#2 → ...

```bash
# gh detecta la base automáticamente si la branch deriva de otra branch
git checkout -b feat/renderer-backend
# ... commits ...
gh pr create --title "feat(renderer): define Backend interface" --body "Part 1/3"

git checkout -b feat/renderer-software
# ... commits ...
gh pr create --title "feat(renderer): implement SoftwareBackend" \
  --body "Part 2/3 — depende de #1" \
  --base feat/renderer-backend
```

---

## Releases

### Versionado — SemVer estricto
| Componente | Cuándo incrementar |
|---|---|
| MAJOR | Breaking change en API pública |
| MINOR | Feature nueva, compatible hacia atrás |
| PATCH | Bug fix, refactor interno, docs |

### Proceso de Release
1. Crear release branch: `release/vX.Y.Z` desde `main`
2. Ultimos checks: `zig build test`, revisar changelog
3. Tag: `git tag vX.Y.Z && git push origin vX.Y.Z`
4. Release en GitHub:

```bash
gh release create vX.Y.Z \
  --title "Infinity Engine vX.Y.Z" \
  --notes-from-tag \
  --target main
```

### Changelog
- Se genera automáticamente de los títulos de PR (conventional commits)
- `feat:` → "Added", `fix:` → "Fixed", `feat!:` → "Changed" (breaking)
- `gh release create --notes-from-tag` genera las notes automáticamente
- No mantener CHANGELOG.md manual — es redundante y se desactualiza

---

## CI/CD

### GitHub Actions — Eventos
| Evento | Workflow |
|---|---|
| `push` a cualquier branch | `build` + `test` |
| `pull_request` contra `main` | `build` + `test` + `lint` |
| `push` tag `v*` | `build` + `test` + `release` |
| `schedule` (semanal) | `long-running-test` |

### Gates de CI
```
┌─ push ──→ build ──→ test ──→ (ok) ──→ ✓
│
┌─ PR ──→ build ──→ test ──→ lint ──→ review ──→ squash merge ──→ ✓
│
┌─ tag v* ──→ build ──→ test ──→ draft release ──→ publish ──→ ✓
```

### Workflows Mínimos
- **`ci.yml`**: `zig build test` + `zig build` en Ubuntu latest
- **`release.yml`**: Disparado por tag `v*`, crea release en GitHub
- **`weekly.yml`**: Tests largos, memory leak detection, benchmark regressions

---

## Branch Strategy — Trunk-Based con Short-Lived Branches
| Branch | De | Merge a | Vida |
|---|---|---|---|
| `main` | — | — | Eterna |
| `feat/*` | `main` | `main` (squash) | < 3 días |
| `fix/*` | `main` | `main` (squash) | < 1 día |
| `release/v*` | `main` | `main` | < 1 semana |
| `chore/*` | `main` | `main` (squash) | < 1 día |

**Regla**: ninguna branch vive más de 3 días sin mergear o descartar. Las branches largas son deuda técnica en forma de merge conflicts.

---

## Automation

### Hooks (local)
- **pre-commit**: `zig build test` (solo los tests del área modificada)
- **commit-msg**: validar formato `type(scope): desc`

### GitHub Actions (automation)
- Auto-assign issues a milestone cuando se crean con label `priority:critical|high`
- Auto-close issues cuando se mergea un PR con `Closes #N`
- Auto-label PRs según el tipo del título (`feat:` → `type:feature`, etc.)
- Stale bot: issues sin actividad por 30 días → `status:stale` → close a los 7 días

```bash
# Ver estado de CI sin salir de la terminal
gh run list --limit 5
gh run view --log
gh pr checks
```

---

## Prohibido
- ❌ Mergear sin review si el PR cambia lógica del engine (excepción: docs, typos, CI config)
- ❌ Push directo a `main` (protegido por branch rules de GitHub)
- ❌ Commits sin conventional commit format
- ❌ PRs de >500 líneas sin aprobación explícita (partir en stacked diffs)
- ❌ Dependabot o PRs automáticos de terceros sin revisión humana

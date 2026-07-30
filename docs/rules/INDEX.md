# Infinity Engine — Rules

Este directorio contiene TODAS las reglas del proyecto. Cada archivo cubre un área específica y debe ser mantenido tan conciso como sea posible.

## Core
| # | Archivo | Área |
|---|---------|------|
| 01 | [01-architecture.md](01-architecture.md) | Límites entre módulos, dirección de dependencias |
| 02 | [02-testing.md](02-testing.md) | Testing first, cobertura, mirror structure |
| 03 | [03-code-style.md](03-code-style.md) | Naming, estructura de archivo, imports |
| 04 | [04-memory.md](04-memory.md) | Allocators explícitos, arena discipline |
| 05 | [05-error-handling.md](05-error-handling.md) | Error sets, propagación, categorías |
| 06 | [06-dependencies.md](06-dependencies.md) | Política de dependencias, build, CI gates |
| 07 | [07-platform.md](07-platform.md) | Abstracción de plataforma, interface/backend |
| 08 | [08-ai-blueprint.md](08-ai-blueprint.md) | AI nativa, Blueprint VM, protocolo de contexto |
| 09 | [09-performance.md](09-performance.md) | Frame budget, optimization process, tracking |

## Engine Domains
| # | Archivo | Área |
|---|---------|------|
| 10 | [10-ecs.md](10-ecs.md) | ECS: componentes, sistemas, queries, entity lifecycle |
| 11 | [11-math-conventions.md](11-math-conventions.md) | Sistema de coordenadas, handedness, matrices |
| 12 | [12-logging.md](12-logging.md) | Logging estructurado, compile-time stripping, diagnostics |
| 13 | [13-api-visibility.md](13-api-visibility.md) | Export tiers, deprecation protocol, @experimental |
| 14 | [14-input.md](14-input.md) | Action mapping, bindings, abstracción de input |
| 15 | [15-renderer.md](15-renderer.md) | Interfaz de backend de renderizado, extensibilidad |
| 16 | [16-github.md](16-github.md) | Issues, PRs, releases, CI/CD, branch strategy |
| 17 | [17-benchmarking.md](17-benchmarking.md) | Hotspots, harness, gates, build integration |

---

**Regla de oro**: si una regla no está escrita, no existe. Si existe pero no se aplica, es deuda técnica.

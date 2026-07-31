# Infinity Engine

> Un motor de videojuegos AAA escrito en C++23 desde cero, con IA nativa, Blueprints first-class y mundo online desde el día 1.

![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat&logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.24%2B-064F8C?style=flat&logo=cmake&logoColor=white)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-0A66C2?style=flat&logo=linux&logoColor=white)
![CI](https://github.com/lucas-r-manoguerra/infinity-engine/actions/workflows/ci.yml/badge.svg)
![Status](https://img.shields.io/badge/status-alpha%20v0.1.0--alpha.2-orange?style=flat)
![License](https://img.shields.io/badge/license-MIT-green?style=flat)

---

## Índice

- [🚀 Sobre el proyecto](#sobre-el-proyecto)
- [✨ Características clave](#características-clave)
- [🗺️ Roadmap](#roadmap)
- [⚡ Empezar en 3 comandos](#empezar-en-3-comandos)
- [📦 Estructura del repositorio](#estructura-del-repositorio)
- [🏗️ Arquitectura](#arquitectura)
- [📜 Reglas de desarrollo](#reglas-de-desarrollo)
- [🧭 Documentación](#documentación)
- [🤝 Contribuir](#contribuir)
- [⚖️ Licencia](#licencia)

---

## 🚀 Sobre el proyecto

Infinity Engine es un motor de videojuegos AAA de grado profesional construido **en C++23 desde cero**, donde cada capa está diseñada para maximizar control, rendimiento y flexibilidad. La IA es parte del engine, no un plugin: entiende el proyecto, genera gameplay desde lenguaje natural, escribe C++ optimizado y compila Blueprints a código nativo sin intermediarios. El diseño arranca con **restricciones de forma** que condicionan todas las decisiones de hoy: juegos online de entrada (MMO/RPG/MMORPG), mundo procedural determinista por seed y un renderer que escala a clase Nanite/Lumen (geometría virtualizada e iluminación global dinámica).

La visión completa vive en [`docs/VISION.md`](docs/VISION.md), el plan de construcción en [`docs/ROADMAP.md`](docs/ROADMAP.md) y el contrato arquitectónico en [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## ✨ Características clave

| | | | |
|---|---|---|---|
| 🤖 **IA nativa** — Agentes integrados que entienden todo el proyecto: generan gameplay desde lenguaje natural, escriben código C++ del engine, compilan Blueprints a nativo y asisten en debugging con contexto completo del runtime. | 🔷 **Blueprints first-class** — Scripting visual que no es azúcar: se compila a código C++ nativo y legible, con los mismos gates de calidad que el código humano. | 🌐 **Online-first (MMO/RPG)** — El server es la verdad del mundo; los clients predicen y rebobinan. Inputs y comandos son la unidad de red, y el mundo es particionable y persistente (ADR-062). | 🌍 **Mundo procedural** — Generación determinista por seed: mismo seed, mismo mundo en todo client, server y test. El mundo abierto se streamea por chunks con presupuesto (ADR-076/077). |
| 🖥️ **Renderer day-1** — Renderer software rasterizado por tiles desde el día 1 (ADR-004), con un esqueleto Vulkan como segundo backend del mismo RHI backend-agnostic (ADR-009). La escena nunca sabe qué backend dibuja. | 🎛️ **Control total en C++** — Cero runtime oculto, cero GC: allocators explícitos por subsistema, errores con `std::expected` (`-fno-exceptions`), cada ciclo de CPU es intencional y medible. | 🧮 **Math determinista** — Right-handed, +Y up, −Z forward, column-major, orden SRT, sin `-ffast-math` (ADR-056): el mismo código produce el mismo resultado en todas las plataformas. | ✅ **Cultura testing-first** — No se escribe una función sin su test. doctest + CTest + sanitizers, y un cambio no está "listo" hasta que `ctest` pasa verde con 0 leaks bajo ASan. |

## 🗺️ Roadmap

Construcción módulo por módulo en orden de dependencia, desde los fundamentos hasta el online server-class. **Cada fase termina con criterios de aceptación explícitos y una demo corriendo** (vertical slice, ADR-060). El plan completo: [`docs/ROADMAP.md`](docs/ROADMAP.md).

| Fase | Nombre | Descripción | Estado |
|---|---|---|---|
| **F0** | Fundamentos | Layout CMake, toolchain C++23, doctest vendored, sanitizers, CI, reglas opencode | ✅ Completada |
| **F1** | Math Core | `Vec2/3/4`, `Mat4`, `Quat`, `Transform` con tests y benchmarks | ✅ Completada |
| **F2** | Core | Allocators explícitos, `std::expected`, fixed timestep 60 Hz, thread pool, fault injection | 🚧 En curso (F2.1–F2.7) |
| **F3** | Platform | Abstracción de plataforma con backend X11 (window, input determinista, gamepad) | ⏳ Planificada |
| **F4** | Renderer | RHI backend-agnostic: backend software BGRA32 por tiles + esqueleto Vulkan + headless | ⏳ Planificada |
| **F5** | ECS | World, Entity (handle + generation), Component, System, Query con read/write sets | ⏳ Planificada |
| **F6** | Runtime + MVP Parity 🏁 | `apps/sandbox`: ventana + triángulo + ECS. El engine corre y se puede mostrar | ⏳ Planificada |
| **F7** | IA | `ContextSnapshot`, Agent, Prompt templates, CodeGen → C++ vía pipeline `tools/` | ⏳ Planificada |
| **F8** | Blueprint | VM + node graph + compilador Blueprint → C++ nativo y legible | ⏳ Planificada |
| **F9** | Asset pipeline | Loader async, mesh, texture, glTF; streaming por chunks, procgen por seed, materiales como data | ⏳ Planificada |
| **F10** | Escena 3D + física + mundo | Scene graph, frustum, física vendored tras interfaz propia, terreno/open world | ⏳ Planificada |
| **F11** | Audio | Streaming, 3D spatial, mixer como sistema del ECS con presupuesto | ⏳ Planificada |
| **F12** | Editor Suite + contenido | Blueprint/level editor, profiler — el editor ES el engine (ADR-064) | ⏳ Planificada |
| **F13** | Optimización + renderer-class | Archetype ECS, job system, targets GPU-driven, geometría virtualizada y GI (ADR-079/080/081) | ⏳ Planificada |
| **F14** | Multi-plataforma + ecosistema | Win32, Cocoa/Metal; semver + changelog + release automation | ⏳ Planificada |
| **F15** | Online / server-class | Netcode server authority, mundo particionable y persistente, red simulada, telemetría, seguridad | ⏳ Planificada |

**Decisiones**: el diseño se documenta en **96 decisiones fundacionales (D1–D96)** y **97 ADRs (ADR-001 a ADR-097)** en `docs/ARCHITECTURE.md`.

**Criterios de salida** (gates de calidad obligatorios): tests verdes + 0 leaks bajo ASan por módulo; benchmarks dentro de los targets documentados (una regresión = bug, no se avanza); `-Werror`, `clang-format` y `clang-tidy` limpios siempre; y demo corriendo por fase (ADR-060). F6 marca el hito **MVP parity**.

**Métricas de progreso y baselines** (medidos en la era previa, targets para el C++ — sección 7 del roadmap):

| Métrica | Target | Métrica | Target |
|---|---|---|---|
| `mat4.mul` | ~34 ns | `entity.create` | ~8 ns |
| `mat4.inverse` | ~18 ns | `query` 10k entidades | ~170 μs |
| `quat.slerp` | ~75 ns | `arena alloc` | <60 ns *(nuevo)* |

Volumen estimado del C++: ~500 líneas en F0, ~4.000 en F1, ~45.000 en F9–F12, ~80.000 en F13–F14 y ~15.000 en F15.

## ⚡ Empezar en 3 comandos

Requisitos: CMake 3.24+, un compilador C++23 (GCC 14+ / Clang 18+) y Ninja.

```bash
cmake --preset debug          # configurar (primera vez o al cambiar CMake)
cmake --build --preset debug  # build completo
ctest --preset debug          # correr TODOS los tests (con sanitizers)
```

Antes de commitear, formatear y lintear:

```bash
./scripts/format.sh           # clang-format + clang-tidy (--check en CI)
```

### Presets

| Preset | Uso | Flags |
|---|---|---|
| `debug` | Desarrollo diario | C++23, `-Wall -Wextra -Werror`, ASan + UBSan, sin optimizar |
| `release` | Benchmarks y entrega | `-O2`, sin sanitizers, `-Werror` |
| `ci` | CI (GitHub Actions) | Idéntico a `debug` + clang-tidy con warnings como errores |

Benchmarks en release: `cmake --preset release && cmake --build --preset release && ./apps/bench/infinity-bench` *(el ejecutable llega en F6)*.

## 📦 Estructura del repositorio

```
Infinity-Engine/
├── ⚙️ CMakeLists.txt / CMakePresets.json   # presets debug / release / ci
├── cmake/            # toolchain, warnings, sanitizers
├── 🔩 engine/         # el engine — CADA módulo = static library
│   ├── core/         # Allocator, Time, Loop, Diagnostics, ThreadPool
│   ├── math/         # Vec2/3/4, Mat4, Quat, Transform
│   ├── platform/     # Window, Input + backends (X11…)
│   ├── ecs/          # World, Entity, Component, System, Query
│   ├── renderer/     # interfaz + backends (software, vulkan)
│   ├── ai/           # ContextSnapshot, Agent, Prompt, CodeGen
│   ├── blueprint/    # VM, Node, Graph, Compiler (→ C++)
│   └── runtime/      # lifecycle del engine: init → run → shutdown
├── 🖥️ apps/           # ejecutables: sandbox (demo), bench (benchmarks)
├── 🧪 tests/          # CTest — espejo de engine/ (doctest)
├── 📦 third_party/    # dependencias vendored (solo doctest por ahora)
├── 🛠️ scripts/        # format.sh, check-licenses.sh
├── 🧰 tools/          # codegen unificado: reflection, blueprint compiler, IA
├── 📖 docs/           # VISION, ROADMAP, ARCHITECTURE, rules/
└── 🎨 assets/         # contenido data-driven: source → cooked → load
```

## 🏗️ Arquitectura

Modularidad por **static libraries** (`infinity_math`, `infinity_core`, …): las reglas de dependencia se cumplen en tiempo de link, no por buena voluntad. Una dependencia ilegal **rompe el build**.

```
apps/            →  infinity_runtime
runtime          →  core, platform, renderer, ecs
renderer         →  core, math, platform
ecs              →  core, math
platform         →  core
ai / blueprint   →  core        (la IA habla con el resto vía ContextSnapshot serializado)
math / core      →  (nada)
```

- **Módulos de bajo nivel no conocen a los de alto nivel**: `math` no sabe que existe `ecs`; `core` no sabe que existe `ai`.
- **La plataforma siempre abstraída**: `platform/window.h` define la interfaz; los backends viven en `src/<backend>/` (X11 por ahora) y se eligen en tiempo de compilación.
- **La IA no importa ECS ni Renderer**: recibe un `ContextSnapshot` serializado (data-only).
- **Tests espejo**: `engine/<mod>/` → `tests/<mod>/`, un ejecutable `infinity_<mod>_tests` por módulo.
- **Un archivo = una tarea**: headers públicos en `engine/<mod>/include/infinity/<mod>/`, lo que no está ahí no existe para los demás módulos.

## 📜 Reglas de desarrollo

Resumen de [`docs/rules/INDEX.md`](docs/rules/INDEX.md) — las reglas son ley:

- 🧪 **Testing First** — no se escribe una función sin su test. Punto.
- 📄 **One File = One Task** — un archivo que pasa ~300 líneas se divide.
- 🚫 **Sin excepciones** — `-fno-exceptions` en todo el proyecto; errores explícitos con `std::expected`.
- 📦 **Dependencias sí, acoplarse nunca** (ADR-061) — todo se vendorea en `third_party/` tras una interfaz propia, con licencia + provenance + why auditados en CI (ADR-068). Nada de FetchContent ni apt-get.
- ✅ **"Listo" significa**: `ctest` verde completo + 0 leaks (ASan) + `./scripts/format.sh` sin diffs.
- 🎲 **Math determinista** (ADR-056) — right-handed, +Y up, −Z forward, SRT, sin `-ffast-math`.
- 🗣️ **Docs en español, código en inglés.**

## 🧭 Documentación

| Documento | Contenido |
|---|---|
| [`docs/VISION.md`](docs/VISION.md) | Visión del producto: control total, IA nativa, tres caminos (IA-only / C++ / Blueprints), online + procedural + renderer-class |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | Plan de construcción F0–F15, decisiones D1–D96, métricas y baselines |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Estructura, dependencias y los 97 ADRs |
| [`docs/rules/`](docs/rules/) | Reglas operativas de desarrollo (01–11) |

## 🤝 Contribuir

1. Leer [`docs/rules/INDEX.md`](docs/rules/INDEX.md) — y la regla específica del módulo que se toca.
2. Escribir el **test primero** (rojo → verde), en espejo en `tests/<mod>/`.
3. **Conventional commits** en inglés (`feat(math): …`, `fix(ecs): …`), PRs pequeños: objetivo < 400 líneas de diff, o **chained PRs** si excede.
4. Chequeo pre-PR obligatorio: `./scripts/format.sh` sin diffs, `ctest --preset debug` verde, `cmake --build --preset ci` verde (tidy + format check), benchmarks si se tocan hot paths.
5. **CI verde es condición para el merge** — un CI rojo se arregla o se revierte, nunca se mergea.

## ⚖️ Licencia

MIT — véase [`LICENSE`](LICENSE). Uso libre, incluido uso comercial, con atribución del copyright.

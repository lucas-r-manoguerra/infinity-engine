# Regla 05 — Build System

> CMake moderno (target-based). Estos son los comandos exactos que usan humanos y opencode.

## Presets

| Preset | Uso | Flags |
|---|---|---|
| `debug` | Desarrollo diario | C++23, `-Wall -Wextra -Werror`, ASan + UBSan, sin optimizar |
| `release` | Benchmarks y entrega | `-O2`, sin sanitizers, asserts off |
| `ci` | GitHub Actions | Idéntico a debug + clang-tidy + clang-format check |

## Comandos (siempre estos, desde la raíz del repo)

```bash
# Configurar (primera vez o cuando cambia CMakeLists)
cmake --preset debug

# Build completo
cmake --build --preset debug

# Build + correr TODOS los tests
ctest --preset debug

# Build de un módulo específico
cmake --build --preset debug --target infinity_math

# Correr tests de un módulo
ctest --preset debug -R math

# Benchmarks (release)
cmake --preset release && cmake --build --preset release && ./apps/bench/infinity-bench

# Formato + lint (obligatorio antes de commit)
./scripts/format.sh
```

## Targets

Cada módulo de `engine/` es un static library: `infinity_math`, `infinity_core`,
`infinity_platform`, `infinity_ecs`, `infinity_renderer`, `infinity_ai`,
`infinity_blueprint`, `infinity_runtime`.

Ejecutables: `sandbox` (engine demo), `infinity-bench` (benchmarks).
Tests: un ejecutable por módulo, `infinity_<mod>_tests`, registrado en CTest.

## Reglas duras

1. **Nunca** correr `g++`/`clang++` a mano para buildear el proyecto. Siempre CMake.
2. **Nunca** agregar dependencias con `FetchContent` ni apt-get. Las dependencias se
   vendorean en `third_party/` **tras una interfaz propia** (ADR-061) — se integran, no
   se reinventan — y se justifican con **licencia + provenance + why** (ADR-068).
   Solo se escribe desde cero lo que es core del engine.
3. `-Werror` activo en todos los presets: un warning es un build rojo.
4. Si opencode necesita otra herramienta (meson, makefile, script propio),
   la propone primero en docs — no la inventa a mitad de camino.

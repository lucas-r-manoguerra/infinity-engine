# Infinity Engine — Reglas de Desarrollo (C++)

> Reglas operativas para trabajar en este repo. Están pensadas para que **opencode**
> (o cualquier agente) pueda leer, modificar y verificar código con precisión.
>
> opencode carga automáticamente estas reglas (configuradas en `opencode.json`).
> **Léelas todas antes de tocar código.** Las reglas son pocas pero son ley.

## Índice

| Regla | Tema | Cuándo aplica |
|---|---|---|
| [01-architecture](01-architecture.md) | Capas, módulos, dependencias | Siempre |
| [02-cpp-language](02-cpp-language.md) | C++23, naming, estructura de archivos | Al escribir código |
| [03-memory](03-memory.md) | Allocators, ownership, leaks | Al escribir código |
| [04-errors](04-errors.md) | `std::expected`, sin excepciones | Al escribir código |
| [05-build](05-build.md) | CMake, presets, comandos exactos | Antes de buildear |
| [06-testing](06-testing.md) | doctest, CTest, test-first | Antes de escribir features |
| [07-math-conventions](07-math-conventions.md) | Sistema de coordenadas, matrices | Al tocar math |
| [08-performance](08-performance.md) | Frame budget, hot paths, benchmarks | Al tocar hot paths |
| [09-opencode-harness](09-opencode-harness.md) | Flujo de verificación del agente | Siempre |
| [10-github](10-github.md) | Commits, PRs, CI | Antes de commitear |
| [11-determinism](11-determinism.md) | Determinismo, RNG, no hidden state | Siempre |

## Reglas transversales (aplican a TODO)

1. **Testing First**: no se escribe una función sin su test. Punto.
2. **One File = One Task**: si un archivo pasa ~300 líneas, se divide.
3. **Dependencias sí, acoplarse nunca** (ADR-061): las dependencias se vendorean en
   `third_party/` tras una interfaz propia — se integran, no se reinventan. Nada de
   apt-get install de librerías, nada de FetchContent, licencia auditada (ADR-068).
4. **Verificar antes de declarar done**: un cambio no está terminado hasta que
   `ctest` pasa verde y ASan no reporta leaks.
5. **Docs en español, código en inglés.**

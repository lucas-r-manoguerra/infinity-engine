# Infinity Engine — Referencias de Inspiración

> Mapa curado de referencias externas (Unreal, Unity, Godot, Blender, Vulkan, Nanite, Lumen, DOTS) para
> orientar a los agentes que construyen este engine. No es un syllabus ni una lista de
> enlaces a motores completos: es una selección por capa, anclada a los ADRs, con qué
> tomamos y qué rechazamos explícito.

## 🎯 Propósito y uso

Este documento orienta a cualquier agente que construya o modifique el engine: cada capa
tiene una referencia externa reconocible y un ADR como fuente de verdad. Si una referencia
contradice un ADR o una regla de `docs/rules/`, manda el ADR — este mapa describe
**inspiración, no contrato**. Se lee por capa (tabla abajo) y se consulta el glosario
cuando un término local confunda. Se actualiza solo cuando cambia una decisión con ADR.

## 🗺️ Mapa de inspiración por capa

Tipo de referencia: **convención de industria** (lo estándar), **inspiración conceptual**
(ideas, no implementaciones) o **divergencia deliberada** (este proyecto hace distinto).

| Área | Referencia (producto + concepto) | Qué tomamos | Qué rechazamos | Ancla |
|---|---|---|---|---|
| ECS | Unity DOTS — *archetype* storage (inspiración conceptual) | Componentes en arrays planos, iteración cache-friendly, migración a archetypes sin cambiar la API pública | El modelo GameObject/Component clásico de Unity; queries por reflexión | ADR-007, ADR-083 |
| Errores | Engines C++ AAA — *no exceptions en hot paths* (divergencia deliberada) | `-fno-exceptions` + `std::expected<T,E>` en toda API fallible | El `check`/`ensure` + excepciones de Unreal; errores escondidos en magic values | ADR-003 |
| Memoria | Tradición de engines C++ — *custom allocators* (Unreal `FMemory`) + arena frame-scoped (convención) | `Allocator` explícito por subsistema, arena por frame, cero alloc en hot paths, budgets | GC (Unity); `new`/`malloc` sueltos; `shared_ptr` en callbacks | ADR-005, ADR-034 |
| Renderer | Vulkan/GL — convención de math y clip space (convención); rasterizador software (ADR-004); Nanite (ADR-080) y Lumen (ADR-081) como targets conceptuales | RHI backend-agnostic; right-handed, column-major, +Y up, −Z forward; escena como data que la GPU pueda consumir | Copiar la implementación de Nanite/Lumen; GPU-driven/bindless como feature tardía | ADR-004, ADR-009, ADR-079, ADR-080, ADR-081 |
| Asset pipeline | Unity — modelo *import/cook* de assets (inspiración conceptual) | `source → cooked → load`, hot reload con manifest, UUID como identidad, streaming por chunks | Asset bundles binarios opacos; contenido en código; identidad por path | ADR-011, ADR-021, ADR-040, ADR-077 |
| Blueprints | Unreal Blueprints — *node graph visual* (inspiración conceptual) | Grafo visual que **compila a C++ nativo**; schema versionado y migrable; content trust | La VM interpretada de Unreal; blueprints como azúcar visual; un lenguaje de scripting nuevo | ADR-022, ADR-042, ADR-095 |
| Editor | Godot (escena/nodos) y Blender (editores integrados) — inspiración conceptual | Editor = **modo del engine** sobre debug UI, render targets y profiler; edita prefabs como data; undo/redo vía comandos | El editor como segunda arquitectura; un "todo-en-viewport" que esconde datos complejos | ADR-064, ADR-035, ADR-039, ADR-041 |
| Math | OpenGL/Vulkan — coordenadas y matrices (convención de industria) | Right-handed, +Y up, −Z forward, column-major, SRT, quats `[x,y,z,w]` | DirectX (left-handed, row-major); `-ffast-math` | Regla 07, ADR-056 |
| Determinismo / netcode | Replay frame-perfect y *rollback netcode* (inspiración conceptual) | RNG por seed, fixed timestep, inputs/comandos como data, server authority, mundo particionable, interés por zona | `rand()`, time-based, client-authoritative, replicar el mundo entero a cada client | ADR-013, ADR-033, ADR-039, ADR-062, ADR-073, ADR-093 |
| Logging | Structured logging moderno (JSON events) (convención) | Niveles, canales por subsistema, sinks; log como data para el crash pipeline | `printf`/`std::cout`; texto plano sin estructura | ADR-046 |
| Build y deps | Ecosistema vcpkg/conan y `FetchContent` (convención, para comparar) | Deps **vendored** en `third_party/` tras **interfaz propia**; licencia + provenance + why auditados en CI | FetchContent, apt-get, package managers; acoplarse a la API de una lib | ADR-061, ADR-068 |

## ✅ Qué tomamos / qué rechazamos

| Tomamos | Rechazamos | Por qué | Ancla |
|---|---|---|---|
| Errores explícitos `std::expected` | Excepciones C++ | Costo impredecible en hot paths; el error queda visible y testeable | ADR-003 |
| Allocators explícitos + budgets | GC / `new` disperso | Cero GC pauses; límites declarados por subsistema | ADR-005, ADR-034 |
| Archetype ECS + read/write sets | Recorrer el mundo por punteros | Iteración sobre arrays planos; paralelización segura sin rediseño | ADR-007, ADR-018 |
| Deps vendored tras interfaz propia | FetchContent / apt-get | Reemplazo = cambiar backend; CI hermética y auditable | ADR-061, ADR-068 |
| Math determinista sin `-ffast-math` | Optimizaciones que rompen IEEE | Mismo resultado en todas las plataformas | ADR-056 |
| Log como data estructurada | `printf` / `std::cout` | Observabilidad + crash pipeline | ADR-046 |
| Blueprint compilado a nativo | Blueprint interpretado | Sin capa extra en runtime; mismo rendimiento que C++ | ADR-022, ADR-095 |
| Editor = modo del engine | Editor como app separada con arquitectura propia | Una arquitectura, un loop, un mundo; las tools son features del runtime | ADR-064 |
| Input y comandos como data | Estado global mutable | Determinismo, replay y red usan la misma unidad | ADR-013, ADR-033, ADR-039, ADR-062 |
| Server authority + interest management | Client-authoritative | El client no es confiable; se replica solo lo relevante | ADR-062, ADR-093, ADR-096 |
| Contenido = data versionada | Contenido en código | Migraciones (ADR-022) y hot reload sin recompilar | ADR-011, ADR-022 |

## 📖 Glosario propio

| Término | En este proyecto | Equivalente industrial |
|---|---|---|
| `ContextSnapshot` | Snapshot serializado data-only del mundo que recibe la IA; la IA no importa ECS/Renderer | Observación de estado / payload de contexto (sin equivalente directo) |
| Archetype | Agrupación de entidades con el mismo set de componentes; almacenamiento plano | Archetype de Unity DOTS / ECS data layout |
| Cooked asset | Formato runtime eficiente generado por el pipeline `source → cooked → load` | Cooked pak (Unreal), cooked asset (Unity) |
| Manifest | Índice de assets: hashes, dependencias, UUID → archivo | AssetRegistry (Unreal), `.meta` (Unity) |
| ReadSet / WriteSet | Qué lee y qué escribe cada sistema; documentación hoy, paralelismo mañana | Declaraciones de dependencia de sistemas (sin equivalente estándar) |
| Comando | Unidad de mutación del mundo: data tipada y versionada | Command pattern; las RPC de red son un caso de uso, no el concepto |
| Scenario | Estado inicial + inputs + objetivo; formato único para test, demo y eval | Golden path / test fixture (sin equivalente directo) |
| Span | Segmento jerárquico de tiempo del profiler, sin allocaciones | Span de tracing (OpenTelemetry) |
| Budget | Presupuesto declarado por subsistema (memoria, tiempo, bandwidth) | Frame budget (Unreal) extendido a memoria y red |
| UUID de asset | Identidad estable de un asset; el path es solo su ubicación | Asset GUID / long ID (Unity, Unreal) |

## 🚫 Lo que este proyecto NO hace

- No usa **excepciones** (`try`/`catch`/`throw`) — errores explícitos con `std::expected` (ADR-003).
- No tiene **estado global mutable ni hidden state** — todo estado vive en el mundo/sistemas (ADR-038).
- No usa **FetchContent ni apt-get** — dependencias vendored y auditadas (ADR-061, ADR-068).
- No usa **`rand()` ni time-based** — RNG por seed reproducible (ADR-013).
- No usa **`-ffast-math`** — determinismo IEEE portable (ADR-056).
- No loggea con **`std::cout`/`printf`** — logging estructurado por canales y sinks (ADR-046).
- No tiene **GC** — allocators explícitos con budgets por subsistema (ADR-005, ADR-034).
- No hay **"lo pruebo después"** — testing first es ley (regla 06).
- No hay **push directo a main** — solo merges de PRs desde ramas secundarias (regla 12).
- No hay **contenido en código** — todo el contenido es data versionada (ADR-011, ADR-022).

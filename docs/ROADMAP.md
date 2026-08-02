# Infinity Engine — Roadmap de Migración a C++

> Camino estructurado desde cero hasta la `docs/VISION.md`, construyendo el motor
> **en C++23 desde cero**.
>
> Este documento **reemplaza cualquier roadmap anterior** y es la fuente de verdad
> para planificación. Se actualiza cuando se toman decisiones; las decisiones pasan a
> `docs/ARCHITECTURE.md`.

---

## 1. Resumen Ejecutivo

Infinity Engine se construye en **C++23 desde cero**, con un layout estándar de la
industria, un harness de tests robusto (doctest + CTest) y reglas de desarrollo
optimizadas para que opencode trabaje con precisión.

| | Decisión |
|---|---|
| Lenguaje | **C++23** (headers clásicos, sin modules) |
| Build | CMake + Ninja + ccache, CMakePresets |
| Tests | **doctest** vendored + CTest + sanitizers |
| Errores | `-fno-exceptions` + `std::expected` |
| Layout | `engine/` + `apps/` + `tests/` + `third_party/` |
| Herencia | Ninguna — el código anterior no existe (clean slate) |

La visión (VISION.md) es el norte: control total, IA nativa, Blueprints first-class,
tres caminos hacia el mismo runtime. Todo lo que se construye apunta a eso.

---

## 2. La Decisión: Por Qué C++ Desde Cero

### 2.1 Por qué C++

1. **Estándar de la industria AAA**: el ecosistema de motores (Unreal, Godot, CryEngine)
   valida C++ para alto rendimiento. Herramientas de profiling, debugging y asset
   pipelines maduran primero para C++.
2. **Madurez de toolchain**: compiladores (GCC/Clang/MSVC), sanitizers (ASan/UBSan/TSan),
   formatters, linters y soporte IDE son décadas más maduros que los de la alternativa anterior.
3. **Ecosistema y talento**: STL, bibliotecas de terceros, contratación de perfiles.

### 2.2 Por qué C++23 (y no C++20)

C++23 no es "C++20 con más features raras" — son **exactamente las features que un
engine nuevo necesita**:

| Feature C++23 | Uso en el engine |
|---|---|
| `std::expected` | Error handling sin excepciones (ADR-003) — es el corazón de la filosofía de errores |
| `std::flat_map` / `std::flat_set` | Containers cache-friendly para el ECS y data-oriented design |
| `deducing this` | Polimorfismo estático sin CRTP — interfaces limpias |
| `std::mdspan` | Vistas multi-dimensionales para matrices/buffers |
| `std::print` | Logging estructurado sin la torpeza de `iostream` |
| Ranges (zip, etc.) | Iteración expresiva sobre arreglos |

**Toolchain**: GCC 14+ / Clang 20+ / MSVC soportan C++23 de forma estable en 2026.
La única excepción deliberada: **no usamos modules (`import`)** — el soporte
cross-compiler sigue inmaduro. Compilación clásica header/source, con C++23 habilitado.

### 2.3 Por qué doctest (y no un harness propio)

| Opción | Veredicto |
|---|---|
| Harness propio | ❌ Reinventar la rueda: sections, parametrización y BDD cuestan meses |
| **doctest vendored** | ✅ Single-header, MIT, funciona con `-fno-exceptions`, integra con CTest |

doctest se **vendorea** en `third_party/doctest/doctest.h` — no es un package manager
ni una dependencia de runtime. Cumple la Dependency Policy (ADR-061): vendored,
aislado y auditado, sin registry, con la potencia de un framework maduro.

### 2.4 Qué se mantiene (no negociable)

- **Pilares de VISION.md**: Control Total, IA Nativa, Tres Caminos Un Motor
- **Arquitectura por capas**: dependencias bottom-up, enforceadas por CMake en link
- **Allocators explícitos** (ADR-005), **fixed timestep** (ADR-006), **ECS con handles + generations** (ADR-007)
- **RHI**: renderer de alto nivel agnóstico del backend; software/Vulkan son backends (ADR-009)
- **Reflection + serialización genérica** (ADR-010), **asset pipeline data-driven** (ADR-011)
- **Modelo de threading formal** (ADR-012), **simulación determinista + replay** (ADR-013)
- **System registry declarativo** (ADR-014)
- **Crash pipeline** (ADR-015), **fault injection** (ADR-016), **property tests + fuzzing** (ADR-017)
- **System read/write sets** (ADR-018), **console + command system** (ADR-019), **config unificada** (ADR-020)
- **Hot reload + manifest** (ADR-021), **schema versioning** (ADR-022), **FS/threads/time + UTF-8** (ADR-023)
- **Codegen unificado `tools/`** (ADR-024), **toolchain hermético** (ADR-025), **docs generadas** (ADR-026)
- **AI control plane** (ADR-027), **AI eval harness** (ADR-028), **código IA = PR** (ADR-029)
- **Headless mode** (ADR-030), **event system como data** (ADR-031), **prefabs** (ADR-032)
- **Input completo determinista** (ADR-033), **memory budgets** (ADR-034)
- **Debug UI immediate-mode** (ADR-035), **profiler jerárquico + frame capture** (ADR-036)
- **Color management sRGB** (ADR-037), **no hidden state** (ADR-038)
- **Mutaciones por comandos** (ADR-039), **assets por UUID** (ADR-040), **render targets** (ADR-041)
- **Content trust boundary** (ADR-042), **glTF intercambio** (ADR-043), **context curation** (ADR-044)
- **Hot reload nativo dev-only** (ADR-045), **structured logging** (ADR-046)
- **Perf dashboard en CI** (ADR-047), **tiempo extendido determinista** (ADR-048), **release automation** (ADR-049)
- **Física vendored tras interfaz** (ADR-050), **cámaras first-class** (ADR-051), **renderer como sistema del ECS** (ADR-052)
- **API stability policy** (ADR-053), **gameplay como plugins** (ADR-054), **time budgets por sistema** (ADR-055)
- **Math determinista sin -ffast-math** (ADR-056), **memory store para IA** (ADR-057), **scenarios formato único** (ADR-058)
- **Packaging reproducible** (ADR-059), **vertical slice como gate de fase** (ADR-060)
- **Dependency policy: usarlas sí, acoplarse nunca** (ADR-061), **networking como restricción** (ADR-062), **save/load = escenario** (ADR-063)
- **El editor ES el engine** (ADR-064), **prompt como data versionada** (ADR-065), **multi-agent especialistas** (ADR-066)
- **Content packs** (ADR-067), **licencias auditadas en CI** (ADR-068), **changelog como input de IA** (ADR-069)
- **Codegen = reflection, un solo modelo del código** (ADR-070), **dev tools first-class** (ADR-071)
- **Persistencia first-class** (ADR-072), **mundo particionable** (ADR-073), **red simulada en eval** (ADR-074), **bandwidth budgets** (ADR-075)
- **Procgen determinista por seed** (ADR-076), **world streaming por chunks** (ADR-077), **IA + procgen comparten pipeline** (ADR-078)
- **Renderer targets: GPU-driven** (ADR-079), **geometría virtualizada** (ADR-080), **global illumination** (ADR-081), **spatial partitioning core** (ADR-082)
- **Escala como requisito de forma** (ADR-083), **animación data-driven** (ADR-084), **gameplay AI como data** (ADR-085)
- **UI declarativa** (ADR-086), **audio como sistema ECS** (ADR-087), **terreno/open world** (ADR-088)
- **Materiales y shaders como data** (ADR-089), **post-processing como pases data** (ADR-090), **i18n desde día 1** (ADR-091)
- **Tuning data-driven** (ADR-092), **interest management** (ADR-093), **telemetría** (ADR-094)
- **Modding = tres caminos** (ADR-095), **seguridad online por diseño** (ADR-096), **compilación como métrica** (ADR-097)
- **One File = One Task** (~300 líneas), **Testing First**, **sin package managers** (todo vendored y auditado — ADR-061)
- **Convenciones matemáticas**: right-handed, +Y up, -Z forward, column-major, SRT
- **Frame budget**: <16.6ms, hot paths sin allocaciones

### 2.5 Clean slate

No hay branch legacy, no hay código anterior, no hay especificaciones de la era previa.
Los únicos datos que sobreviven son los **baselines de rendimiento medidos** (sección 7),
que definen los targets de rendimiento del C++.

---

## 3. Estrategia de Construcción

```
1. Fundamentos (F0): layout, toolchain, doctest, CI, reglas opencode
2. Módulos en ORDEN de dependencia, cada uno un static library target:
   math → core → platform → ecs → renderer → runtime → ai → blueprint → assets
3. Cada módulo termina con: tests verdes, 0 leaks (ASan), benchmarks publicados
4. Cuando runtime + renderer + ecs funcionen → "MVP parity" → el engine corre en C++
5. AI y Blueprint se implementan directamente en C++ (interfaces definidas en F0)
```

Regla de oro: **no se construye nada arriba de una capa sin tests verdes en la capa**.

---

## 4. Decisiones Técnicas Fundacionales

| # | Decisión | Justificación |
|---|---|---|
| D1 | **C++23** | Ver sección 2.2 — features que el engine necesita, toolchain estable |
| D2 | **CMake + Ninja + ccache + presets** | Estándar multiplataforma, CTest, IDE/CI. Cada módulo = static library target → las reglas de dependencia se cumplen solas en link |
| D3 | **`-fno-exceptions` + `std::expected`** | ADR-003. Errores explícitos y verificables, sin costo impredecible |
| D4 | **doctest vendored** (`third_party/doctest/`) | Sección 2.3 — framework maduro, zero registry deps |
| D5 | **Layout `engine/` + `apps/` + `tests/` + `third_party/`** | Estándar de la industria para proyectos C++ escalables; público/privado por módulo; tests espejo |
| D6 | **Sanitizers en debug** | ASan + UBSan activos desde F0: la memoria se trackea desde el día 1 |
| D7 | **`clang-format` + `clang-tidy` en CI** | Formato y lint automáticos — el código siempre es consistente, los PRs son revisables |
| D8 | **RHI — backend-agnostic renderer** | ADR-009. Escena/pipeline no conocen el backend; software hoy, Vulkan mañana |
| D9 | **Reflection + serialización genérica** | ADR-010. Habilita editor, blueprints, ContextSnapshot, save/load sin tocar cada componente |
| D10 | **Asset pipeline data-driven** | ADR-011. Contenido = data (source → cooked → load), nunca código |
| D11 | **Modelo de threading formal** | ADR-012. Límites main/render/jobs desde F0, IO async desde el inicio |
| D12 | **Determinismo + replay** | ADR-013. RNG por seed, simulación reproducible, replay como herramienta de debug/tests |
| D13 | **System registry declarativo** | ADR-014. Init/shutdown resuelto por dependencias, no por orden manual |
| D14 | **Crash pipeline** | ADR-015. Crash → reporte estructurado (backtrace, inputs, snapshot) → IA diagnóstica |
| D15 | **Fault injection** | ADR-016. Fallos inyectables en tests: cada rama de error se ejercita |
| D16 | **Property tests + fuzzing** | ADR-017. Invariantes (math, serialización) con entradas aleatorias |
| D17 | **System read/write sets** | ADR-018. Cada sistema declara qué lee/escribe; el scheduler futuro paraleliza sin rediseño |
| D18 | **Console + command system** | ADR-019. Comandos registrables: debug manual + la IA maneja el engine |
| D19 | **Config unificada** | ADR-020. Precedencia: consola > CLI > archivo > defaults |
| D20 | **Hot reload + manifest** | ADR-021. Assets con hashes/deps; recarga en caliente sin reiniciar |
| D21 | **Schema versioning** | ADR-022. Todo formato de data lleva versión + migración explícita |
| D22 | **FS/threads/time + UTF-8** | ADR-023. Interfaces agnósticas en core; multiplataforma sin reescritura |
| D23 | **Codegen unificado `tools/`** | ADR-024. Reflection, blueprint compiler e IA comparten pipeline de generación |
| D24 | **Toolchain hermético** | ADR-025. Compiladores pinnados, devcontainer/Docker, CI reproducible |
| D25 | **Docs generadas en CI** | ADR-026. Doc de API desde headers; CI falla si desincronizada |
| D26 | **AI control plane** | ADR-027. Capability manifest + permisos + audit log inmutable |
| D27 | **AI eval harness** | ADR-028. Escenarios dorados con veredicto automático — la IA se testea |
| D28 | **Código IA = PR** | ADR-029. El código generado por IA pasa los mismos gates que el humano |
| D29 | **Headless mode** | ADR-030. Backend null del RHI: CI y la IA corren el engine real sin ventana |
| D30 | **Event system como data** | ADR-031. Eventos tipados, versionados, observables por IA/blueprints |
| D31 | **Prefabs data-driven** | ADR-032. Plantillas de composición; el editor edita prefabs, no código |
| D32 | **Input completo determinista** | ADR-033. Gamepad, rebinding, cola determinista (replay = inputs) |
| D33 | **Memory budgets** | ADR-034. Presupuesto por subsistema; superarlo = alerta visible |
| D34 | **Debug UI immediate-mode** | ADR-035. Overlays, inspector, consola; base del editor futuro |
| D35 | **Profiler jerárquico + frame capture** | ADR-036. Spans sin allocaciones desde F0; primera feature del editor |
| D36 | **Color management sRGB** | ADR-037. Linear → sRGB desde el triángulo #1 |
| D37 | **No hidden state** | ADR-038. Todo estado en el mundo/sistemas; habilita networking y replay |
| D38 | **Mutaciones por comandos** | ADR-039. Una unidad de cambio: undo/redo (editor) + audit (IA) + replay (determinismo) |
| D39 | **Assets por UUID** | ADR-040. Identidad estable, path solo ubicación; renombrar no rompe |
| D40 | **Render targets** | ADR-041. Dibujar a texturas desde F4: editor, minimaps, IA visual |
| D41 | **Content trust boundary** | ADR-042. Contenido confiable vs sandboxeado; mods y contenido IA seguros |
| D42 | **glTF intercambio** | ADR-043. Importador propio; un solo formato de entrada 3D |
| D43 | **Context curation** | ADR-044. Snapshot priorizado por presupuesto de tokens; la IA entiende la escena |
| D44 | **Hot reload nativo dev** | ADR-045. Rebuild → reload shared lib → engine vivo; loop de IA en segundos |
| D45 | **Structured logging** | ADR-046. Niveles, canales, sinks; cero printf; alimenta crash pipeline |
| D46 | **Perf dashboard CI** | ADR-047. Curvas por métrica; regresión gradual se detecta sola |
| D47 | **Tiempo extendido** | ADR-048. Time scale/pause/slow-mo deterministas; parte de la simulación |
| D48 | **Release automation** | ADR-049. Semver + changelog generado + build ID (trazabilidad crash → release) |
| D49 | **Física vendored tras interfaz** | ADR-050. Librería en `third_party/` detrás de un contrato propio (patrón RHI); reemplazo = cambiar backend |
| D50 | **Cámaras first-class** | ADR-051. Múltiples cámaras con render targets; se diseña con el renderer |
| D51 | **Renderer como sistema del ECS** | ADR-052. Culling/submit con read/write sets; un scheduler paraleliza render+gameplay |
| D52 | **API stability** | ADR-053. Semver: minor nunca rompe; deprecación con ventana; breaking solo major |
| D53 | **Gameplay como plugins** | ADR-054. Engine = host, gameplay = shared libs; boundary desde F0, mods por diseño |
| D54 | **Time budgets por sistema** | ADR-055. ms/frame máximo por sistema; superarlo es bug visible |
| D55 | **Math determinista** | ADR-056. Sin -ffast-math; política NaN/Inf explícita; mismo resultado en todas las plataformas |
| D56 | **Memory store IA** | ADR-057. El engine guarda/recupera contexto del proyecto y lo inyecta curado |
| D57 | **Scenarios formato único** | ADR-058. Estado inicial + inputs + objetivo: test + demo + eval son el mismo data |
| D58 | **Packaging reproducible** | ADR-059. Binario + assets cooked + libs + licencias; distribuir es un script |
| D59 | **Vertical slice por fase** | ADR-060. Cada fase termina con demo corriendo; el motor siempre jugable |
| D60 | **Dependency policy** | ADR-061. Usar dependencias sí, acoplarse nunca: vendored tras interfaz propia; reemplazo = cambiar backend |
| D61 | **Networking como restricción** | ADR-062. Gameplay diseñado para online desde el día 1 (MMO/RPG/MMORPG): server authority, inputs/comandos = unidad de red, mundo particionable y persistente |
| D62 | **Save/load = escenario** | ADR-063. Save = estado + cola de comandos; cargar = replay; cero formatos paralelos |
| D63 | **El editor ES el engine** | ADR-064. Un modo del engine con herramientas; no existe segunda arquitectura |
| D64 | **Prompt como data** | ADR-065. Prompts versionados, testeados en eval, evolucionan por PRs |
| D65 | **Multi-agent especialistas** | ADR-066. Roles separados con presupuestos de contexto; revisor ≠ autor; eval al equipo |
| D66 | **Content packs** | ADR-067. Creadores fuera del repo; packs versionados con manifest + UUIDs |
| D67 | **Licencias auditadas** | ADR-068. Auditoría en CI: licencia, provenance, why; incompatibilidad = CI rojo |
| D68 | **Changelog input IA** | ADR-069. La IA indexa el changelog antes de tocar APIs deprecadas |
| D69 | **Codegen = reflection** | ADR-070. Un modelo del código (AST + registro) para reflection, blueprints e IA |
| D70 | **Dev tools first-class** | ADR-071. tools/ son producto: documentados, testeados, con dueño |
| D71 | **Persistencia first-class** | ADR-072. El mundo puede guardarse y continuar; persistencia = escenario (ADR-063) ampliado al server |
| D72 | **Mundo particionable** | ADR-073. Zonas/shards: la simulación online se divide, nunca se serializa entera |
| D73 | **Red simulada en eval** | ADR-074. Latencia/pérdida/jitter simulados en CI: el networking se prueba contra la realidad |
| D74 | **Bandwidth budgets** | ADR-075. Bytes/segundo por sistema; superarlos es bug visible como el frame budget |
| D75 | **Procgen con seed como contrato** | ADR-076. Mismo seed → mismo mundo; la generación es data determinista |
| D76 | **World streaming por chunks** | ADR-077. Mundo abierto por bloques con presupuesto; jamás el mundo entero en memoria |
| D77 | **IA + procgen comparten pipeline** | ADR-078. Un pipeline de generación para autoría humana, procedural e IA |
| D78 | **GPU-driven rendering** | ADR-079. Target de renderer: draw calls como data para GPU, no comandos por objeto |
| D79 | **Geometría virtualizada** | ADR-080. Target Nanite-like: mesh con LOD jerárquico, pageable por visibilidad |
| D80 | **Global illumination** | ADR-081. Target Lumen-like: iluminación global dinámica; condiciona el modelo de luz desde F4 |
| D81 | **Spatial partitioning core** | ADR-082. Broad-phase de todo (física, culling, streaming, queries) en core |
| D82 | **Escala como requisito de forma** | ADR-083. Cada sistema se diseña para N entidades/shards; los límites se declaran, no se descubren |
| D83 | **Animación data-driven** | ADR-084. Skinning y clips como data; el motor evalúa, no hardcodea |
| D84 | **Gameplay AI como data** | ADR-085. Behavior trees/GOAP/nav como data; la IA de gameplay se edita, no se recompila |
| D85 | **UI declarativa** | ADR-086. UI de juego como data (widgets/estilos); toolkits third_party vendored tras interfaz |
| D86 | **Audio como sistema ECS** | ADR-087. Listener, emitters y mixer como entidades; presupuesto y determinismo como todo |
| D87 | **Terreno y mundo abierto** | ADR-088. Terreno como data procedural + streaming (ADR-076/077); diseño desde F9 |
| D88 | **Materiales y shaders como data** | ADR-089. Shaders compilados offline, PSO caching; cero hitching en runtime |
| D89 | **Post-processing como pases data** | ADR-090. Pipeline de efectos declarativo sobre render targets (ADR-041) |
| D90 | **Localización desde día 1** | ADR-091. i18n en strings del engine y del gameplay; ningún string hardcodeado |
| D91 | **Tuning data-driven** | ADR-092. Balance en data con versionado; ajustar balance no recompila |
| D92 | **Interest management** | ADR-093. El server replica lo relevante por entidad/jugador, no el mundo |
| D93 | **Telemetría y analytics** | ADR-094. Métricas de juego como data con pipeline propio (ADR-046/047) |
| D94 | **Modding = tres caminos** | ADR-095. Plugins nativos (ADR-054), data/cooked e IA; sin scripting nuevo |
| D95 | **Seguridad online por diseño** | ADR-096. Client = terminal no confiable; el server valida y manda (ADR-062) |
| D96 | **Compilación a escala** | ADR-097. Tiempo de build es métrica de CI con presupuesto; el crecimiento se monitorea |

### Layout objetivo

```
Infinity-Engine/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/                 # toolchain, helpers, sanitizers
├── engine/                # cada módulo = static library (public include/ + src/)
│   ├── core/  math/  platform/  ecs/  renderer/  ai/  blueprint/  runtime/
├── apps/                  # ejecutables: sandbox, bench
├── tests/                 # CTest, espejo de engine/ (doctest)
├── third_party/           # solo doctest por ahora
├── tools/                 # codegen unificado (ADR-024): reflection, blueprint compiler, IA
├── scripts/               # format, lint, pre-commit
└── docs/
```

---

## 5. Fases

> Cada fase: **objetivo** → **justificación** → **tareas pequeñas** → **criterios de aceptación** → **dependencias**.

### F0 — Fundamentos (1-2 semanas)

**Objetivo**: repo listo para C++, harness funcional, reglas opencode en su lugar.

**Justificación**: sin base sólida, todo lo que sigue se construye sobre arena.

**Tareas**:
- [ ] F0.1 Layout CMake completo (`engine/`, `apps/`, `tests/`, `third_party/`, `cmake/`, `tools/`, `CMakePresets.json`)
- [ ] F0.2 Toolchain: C++23, `-Werror`, Ninja, ccache
- [ ] F0.3 Vendorear doctest + test de humo (`apps/` vacío, `tests/core/` con 1 test → `ctest` verde)
- [ ] F0.4 Sanitizers (ASan/UBSan) en preset debug
- [ ] F0.5 CI (GitHub Actions): build + test + sanitizer + clang-tidy + clang-format check
- [ ] F0.6 `clang-format` + `clang-tidy` configs + script `scripts/format.sh`
- [ ] F0.7 Toolchain hermético (ADR-025): devcontainer/Docker con compiladores pinnados + CI reproducible
- [ ] F0.8 Verificar que opencode corre el harness: `cmake --build` + `ctest` desde cero
- [ ] F0.9 Auditoría de licencias en CI (ADR-068): cada dependencia declara licencia + provenance + why
- [ ] F0.10 Dev tools first-class (ADR-071): `scripts/` y `tools/` son producto — documentados y testeados desde el día 1

**Criterios de aceptación**: `cmake --preset debug && cmake --build && ctest` verdes;
`-Werror` limpio; un cambio de ejemplo pasa format → build → test sin fricción.

**Dependencias**: ninguna.

---

### F1 — Math Core

**Objetivo**: `Vec2/Vec3/Vec4/Mat4/Quat/Transform` con tests y benchmarks.

**Justificación**: es la base de todo (cero deps) y el banco de pruebas ideal de la
metodología: operaciones deterministas, fáciles de validar.

**Tareas**:
- [x] F1.1 Tipos SIMD-ready (`alignas(16)`, data flat `[16]f32` column-major)
- [x] F1.2 Vec3/Vec4: operaciones + normalización
- [x] F1.3 Mat4: multiplicación, inversa, transpose, proyecciones
- [x] F1.4 Quat: slerp, normalize, conversión YPR (grados API / radianes interno)
- [x] F1.5 Transform TRS (SRT order)
- [x] F1.6 Benchmarks math (`apps/bench`, Release)
- [x] F1.7 Tests con doctest para TODAS las operaciones públicas
- [x] F1.8 Math determinista (ADR-056): sin `-ffast-math`, política NaN/Inf explícita, invariantes portable

**Criterios**: 100% de operaciones públicas testeadas, 0 leaks (ASan), benchmarks
dentro de los targets documentados (mat4.mul ~34ns, inverse ~18ns, quat.slerp ~75ns)
y **mismos resultados en todas las plataformas** (ADR-056).

**Dependencias**: F0.

---

### F2 — Core (memoria, tiempo, loop, errores)

**Objetivo**: allocators explícitos, `std::expected` como contrato de errores, fixed timestep.

**Justificación**: sin memoria predecible no hay motor; esta capa define la disciplina
de todas las demás (ADR-005).

**Tareas**:
- [x] F2.1 Interfaz `Allocator` (abstract, explícita en cada subsistema)
- [x] F2.2 `ArenaAllocator` + `PoolAllocator` (alineación correcta desde el día 1)
- [x] F2.3 Error sets por subsistema con `std::expected` (sin excepciones)
- [x] F2.4 Fault injection (ADR-016): fallos inyectables en alloc/IO/init para tests
- [x] F2.5 `Time` (clock, delta, FPS) + `Loop` fixed timestep 60Hz (ADR-006)
- [x] F2.6 `Diagnostics` (contadores atómicos, sin allocaciones)
- [x] F2.7 `ThreadPool` (workers idle bloqueados en futex, spawn/wait)
- [ ] F2.8 Abstracción FS/threads/time + UTF-8 (ADR-023) en core — UTF-8 (F2.8a): `Utf8View` + `CoreError::INVALID_UTF8` listos; FS (F2.8b): interfaz `FileSystem` + backend in-memory con fault injection listos (backends OS en F3.5)
- [x] F2.9 Property-based tests + fuzzing base (ADR-017): invariantes de math y round-trips
- [x] F2.10 Benchmarks de memoria (arena alloc objetivo <60ns): medido 5.3ns/op en release (CPU Ryzen 5 7600, clang 20), 11x bajo el target
- [x] F2.11 `SystemRegistry` declarativo (ADR-014): init/shutdown por dependencias con detección de ciclos
- [x] F2.12 Memory budgets por subsistema (ADR-034): `BudgetAllocator` decorator + `MemoryBudgets` tracker; alerta por callback (log/dev console futuros, ADR-046/035)
- [x] F2.13 Profiler jerárquico base (ADR-036): `SpanId` catalog + `Profiler` spans jerárquicos zero-alloc + frame capture de buffer fijo; time source inyectable para tests deterministas
- [x] F2.14 Time budgets por sistema (ADR-055): TimeBudgets tracker — budget máx por SpanId, verificación contra la frame capture del Profiler, alerta por callback + contador en Diagnostics

**Criterios**: tests verdes incluyendo stress de allocators; 0 leaks bajo ASan.

**Dependencias**: F1.

---

### F3 — Platform (X11, window, input)

**Objetivo**: abstracción de plataforma con backend X11 funcional.

**Justificación**: el renderer necesita a quién presentarle; la plataforma se abstrae
primero para no acoplar renderer a X11.

**Tareas**:
- [ ] F3.1 Interfaz `Window` + backend X11 (resize, close) — headless listo (ADR-030); backend X11 pendiente
- [x] F3.2 Interfaz `Input` + action mapping (bindings configurables)
- [ ] F3.3 Input completo (ADR-033): gamepad/joystick, rebinding runtime, cola determinista
- [x] F3.4 Contexto de plataforma (init/cleanup seguro, RAII)
- [ ] F3.5 Backends de FS/threads/time para Linux (sobre core — ADR-023)
- [ ] F3.6 Tests: apertura/cierre de ventana, ciclo de input — headless cubierto; verificación X11 cuando exista el backend

**Criterios**: ventana X11 abre y cierra; input llega al runtime sin acoplar X11.

**Dependencias**: F2.

---

### F4 — Renderer (software → Vulkan)

**Objetivo**: interfaz de renderer con backend software BGRA32 + esqueleto Vulkan.

**Justificación**: el módulo más visible; el backend software permite iterar sin GPU
(ADR-004) mientras Vulkan madura.

**Tareas**:
- [x] F4.1 RHI — interfaz `Renderer` (ADR-009): draw list, clear, draw, present; backend-agnostic
      (F4.1, `engine/renderer/include/infinity/renderer/`, factory `createRenderer`)
- [x] F4.2 `SoftwareBackend`: framebuffer BGRA32, clear, drawTriangle (detrás de la RHI)
- [x] F4.3 Backface culling (correcto desde el día 1) — winding screen-space, configurable
- [x] F4.4 Tile renderer multi-thread (checksums por tiles) — `ThreadPool`, path MT == serial
- [ ] F4.5 `VulkanBackend` esqueleto + loader (mismo contrato RHI)
- [ ] F4.6 `NullBackend` headless (ADR-030): el backend software ya es la ruta CI-hermética sin
      ventana; falta el NullBackend de medición de overhead (documentado en `renderer.h`)
- [x] F4.7 Color management sRGB (ADR-037): linear → sRGB en presentación, desde el día 1
- [ ] F4.8 Render targets (ADR-041): el `RenderTarget` + `present(target)` ya dibujan offscreen;
      falta el uso desde runtime/editor (cámaras → targets)
- [x] F4.9 Tests de renderer (checksums de framebuffer) + benchmark de triangle fill
      (`apps/bench/bench_triangle.cpp`; ver §7)
- [x] F4.10 Cámaras first-class (ADR-051): `Camera` como dato del mundo (posición, rotación,
      fov, aspect, near/far) + `buildViewProjection` y `projectWorldToScreen` en
      `infinity/renderer/camera.h`; la cámara → render target y las múltiples cámaras con
      capas llegan con el ECS (F5, ADR-052)

**Progreso F4 (branches feat/f4-renderer → feat/f4-cameras)**: interfaz RHI + `RenderTarget`
BGRA32 move-only con `checksum()` FNV-1a-64 determinista; backend software por tiles con
half-space scan, culling screen-space y color interpolado; sRGB exactamente una vez por pixel
en present (ADR-037); path multi-thread (tiles disjuntos) produce checksum idéntico al serial
(rule 11); 0 alloc en el hot path (crecimiento → `ALLOCATION_FAILED`); cámara first-class
(ADR-051) como dato serializable (ADR-038): vista/proyección column-major con fov en grados,
mapping world → pixel top-left con +y hacia abajo, rechazo de puntos fuera del frustum /
detrás de cámara / targets degenerados, y validación de parámetros (`INVALID_ARGUMENT`,
rule 04); 32 test cases de renderer + 13 de cámara verdes con ASan sin leaks.

**Criterios**: checksums deterministas; renderer software correcto y testeado;
**triángulo visible** (ADR-060: vertical slice de F4).

**Dependencias**: F3.

---

### F5 — ECS

**Objetivo**: World, Entity (handle + generation), Component, System, Query.

**Justificación**: el ECS es el corazón del gameplay; MVP simple con interfaz estable
para migrar a archetypes después (ADR-007).

**Tareas**:
- [ ] F5.1 `Entity` handle (u32 index + generation, sin punteros)
- [ ] F5.2 `ComponentRegistry` (type-safe, data-only, reflection-aware — ADR-010)
- [ ] F5.3 `World` (spawn/destroy, comandos diferidos)
- [ ] F5.4 `System` (una preocupación, deps explícitas + **read/write sets** — ADR-018)
- [ ] F5.5 `Query` (con fast-path para mundo vacío)
- [ ] F5.6 Schema versioning de componentes/saves (ADR-022): versiones + migraciones
- [ ] F5.7 Event system como data (ADR-031): eventos tipados, versionados, observables
- [ ] F5.8 Prefabs (ADR-032): plantillas data-driven de composición de entidades
- [ ] F5.9 No hidden state (ADR-038): tests de replay doble → mismo resultado
- [ ] F5.10 Comandos como unidad de cambio (ADR-039): mutaciones tipadas, auditables, replay-friendly
- [ ] F5.11 Benchmarks: query.iterate 10k entidades (target ~170μs) + empty fast-path (<5μs)
- [ ] F5.12 Renderer como sistema del ECS (ADR-052): el renderer declara read/write sets y vive en el mundo
- [ ] F5.13 Save/load = escenario (ADR-063): estado + cola de comandos; cargar = replay hasta el frame actual

**Criterios**: tests ECS verdes; query vacía sin overhead.

**Dependencias**: F2.

---

### F6 — Runtime + MVP Parity 🏁

**Objetivo**: `apps/sandbox` orquesta engine → ventana + triángulo + ECS.

**Justificación**: el primer hito jugable: el engine ya no es un conjunto de librerías,
es un motor.

**Tareas**:
- [ ] F6.1 `Engine` (init → run → shutdown vía SystemRegistry — ADR-014)
- [ ] F6.2 `apps/sandbox`: ventana, triángulo, ESC sale
- [ ] F6.3 `apps/bench`: runner de benchmarks
- [ ] F6.4 Modelo de threading formal en marcha (ADR-012): main/render/jobs, IO async
- [ ] F6.5 Determinismo + replay (ADR-013): seed por frame, grabación de inputs/eventos
- [ ] F6.6 Crash pipeline (ADR-015): crash/assert → reporte con backtrace + inputs + snapshot
- [ ] F6.7 Console + command system (ADR-019) + config unificada (ADR-020)
- [ ] F6.8 Debug UI immediate-mode (ADR-035): overlays, inspector, consola en ventana
- [ ] F6.9 Headless completo (ADR-030): sandbox corre el engine real sin ventana
- [ ] F6.10 Structured logging (ADR-046): niveles, canales, sinks; alimenta crash pipeline
- [ ] F6.11 Tiempo extendido (ADR-048): time scale, pause, slow-mo deterministas
- [ ] F6.12 CI completo: build + test + sanitizers + docs generadas (ADR-026) en cada push
- [ ] F6.13 Perf dashboard en CI (ADR-047): curvas por métrica desde F1 en adelante
- [ ] F6.14 Time budgets por sistema (ADR-055): ms/frame máximos declarados + verificados
- [ ] F6.15 Gameplay como plugins (ADR-054): sandbox compilado como shared lib, engine como host
- [ ] F6.16 Packaging reproducible (ADR-059): binario + runtime libs + licencias desde F6
- [ ] F6.17 Contrato de red diseñado (ADR-062): server authority, inputs/comandos como unidad de red, mundo particionable — restricción de forma, no feature aún

**Criterios (MVP PARITY)**: `./sandbox` abre ventana con triángulo, ESC cierra;
`ctest` verde con 0 leaks; benchmarks publicados contra targets; **el MVP corre y se puede mostrar** (ADR-060).

**Dependencias**: F4, F5.

---

### F7 — IA (ContextSnapshot, agent, prompt, codegen)

**Objetivo**: capa de IA nativa: contexto serializado, agente, prompts, generación de C++.

**Justificación**: la IA no es afterthought — el `ContextSnapshot` define el contrato
desde el día 1, aunque el LLM se conecte después.

**Tareas**:
- [ ] F7.1 `ContextSnapshot` (data-only, serializado — la IA NUNCA importa subsistemas)
- [ ] F7.2 `Agent` + config
- [ ] F7.3 `Prompt` templates
- [ ] F7.4 `CodeGen` → C++ (vía pipeline `tools/` — ADR-024: mismo parseo/registro/revisión que reflection y blueprints)
- [ ] F7.5 Integración LLM (model-agnostic)
- [ ] F7.6 Crash pipeline → diagnóstico IA (ADR-015): el reporte de crash alimenta al agente
- [ ] F7.7 AI control plane (ADR-027): capability manifest + permisos + audit log
- [ ] F7.8 AI eval harness (ADR-028): escenarios dorados con veredicto automático
- [ ] F7.9 Código IA = PR (ADR-029): gates idénticos al humano, rollback automático
- [ ] F7.10 La IA valida headless (ADR-030): ejecuta escenarios en el engine real sin ventana
- [ ] F7.11 Memory store interno (ADR-057): el engine guarda/recupera contexto del proyecto y lo inyecta curado (ADR-044)
- [ ] F7.12 Scenarios formato único (ADR-058): test de regresión + demo + escenario dorado del eval (ADR-028) son el mismo data
- [ ] F7.13 Prompts como data versionada (ADR-065): viven en el repo, schema ADR-022, testeados en el eval (ADR-028)
- [ ] F7.14 Multi-agent (ADR-066): especialistas coordinados por orquestador, cada uno con su presupuesto de contexto (ADR-044)
- [ ] F7.15 Changelog como input de IA (ADR-069): el memory store (ADR-057) indexa el changelog para no reintroducir APIs deprecadas

**Criterios**: snapshot serializable/deserializable; tests de round-trip.

**Dependencias**: F2 (y ECS vía snapshot, sin import directo).

---

### F8 — Blueprint (VM, graph, compiler)

**Objetivo**: VM + node graph + compilador Blueprint → C++.

**Justificación**: Blueprints son código real, no azúcar (promesa VISION): el compilador
produce C++ legible.

**Tareas**:
- [ ] F8.1 `Node` + `Graph` (type-checked a edición, no runtime)
- [ ] F8.2 VM de ejecución
- [ ] F8.3 Compilador Blueprint → C++ nativo, legible (vía pipeline `tools/` — ADR-024)
- [ ] F8.4 Schema versioning de blueprints (ADR-022): versiones + migraciones
- [ ] F8.5 Tests: graph → C++ → compila → corre

**Criterios**: un blueprint de ejemplo genera C++ que compila y ejecuta igual.

**Dependencias**: F2.

---

### F9+ — El Camino Largo

| Fase | Objetivo | Depende de |
|---|---|---|
| F7 | IA (ContextSnapshot, agent, prompt, codegen — vía pipeline `tools/` ADR-024) | F6 |
| F8 | Blueprint (VM, graph, compiler — vía pipeline `tools/` ADR-024) | F6 |
| F9 | Asset pipeline (async loader, mesh, texture — hot reload + manifest ADR-021, UUID ADR-040, glTF ADR-043, content packs ADR-067, diseño desde F0 ADR-011; **streaming por chunks ADR-077, procgen por seed ADR-076, materiales/shaders como data ADR-089**, terreno diseñado ADR-088) | F6 |
| F10 | Escena 3D + física + mundo (scene graph, frustum, broad/narrowphase — **física vendored tras interfaz ADR-050/061**, cámaras first-class ADR-051, **spatial partitioning core ADR-082, animación data-driven ADR-084, terreno/open world ADR-088**) | F5, F9 |
| F11 | Audio (streaming, 3D spatial, mixer — **como sistema del ECS ADR-087**) | F2 |
| F12 | Editor Suite + contenido (blueprint editor, level editor, profiler — **el editor ES el engine ADR-064**, sobre debug UI ADR-035, render targets ADR-041, profiler ADR-036 y cámaras ADR-051; **gameplay AI como data ADR-085, UI declarativa ADR-086, post-processing ADR-090, tuning data-driven ADR-092, i18n ADR-091, modding tres caminos ADR-095**) | F8, F9, F10 |
| F13 | Optimización + renderer-class (archetype ECS, job system — usa read/write sets ADR-018; **targets GPU-driven ADR-079, geometría virtualizada ADR-080, GI ADR-081**) | F6+ |
| F14 | Multi-plataforma (Win32, Cocoa/Metal — sobre FS/threads/time ADR-023) + release/ecosistema (semver + changelog ADR-049; **compilación a escala como métrica ADR-097**) | F13 |

> **Infraestructura adelantada (2026-07)**: la CI ya compila y testea `core`+`math` en Linux (gcc-14), macOS (clang-18 vía Homebrew) y Windows (clang-cl-18) en cada push. Queda el backend de plataforma (Win32/Cocoa) y el cableado MSVC nativo + sanitizers/tidy para esa fase.
| F15 | Online / server-class (netcode con server authority ADR-062; **mundo particionable y persistente ADR-072/073, red simulada ADR-074, bandwidth budgets ADR-075, interest management ADR-093, telemetría ADR-094, seguridad online ADR-096**) | F10, F12 |

Cada una se detalla cuando se acerca; los principios y métricas aplican igual.

---

## 6. Gates de Calidad (obligatorios)

1. **Por módulo**: tests verdes (doctest + CTest) y 0 leaks bajo ASan antes de avanzar.
2. **Por rendimiento**: benchmarks dentro de los targets documentados. Más lento = se investiga ANTES de seguir.
3. **En F6**: MVP parity — el engine corre completo.
4. **Siempre**: `clang-format` limpio, `clang-tidy` sin warnings nuevos, `-Werror` verde.
5. **Por fase (ADR-060)**: vertical slice — cada fase termina con una demo corriendo, no solo tests verdes.

---

## 7. Métricas de Progreso y Baselines

### Targets de rendimiento (baselines medidos)

```
mat4.mul         ~34ns      vec3 ops      ~1ns
mat4.inverse     ~18ns      quat.slerp    ~75ns
entity.create    ~8ns       query 10k     ~170μs
query empty      <5μs (target nuevo)      arena alloc  <60ns (target nuevo)
renderer.triangle ~11.1μs (medido F4.9)   renderer.pixel ~20.4ns (medido F4.9)
renderer.full_frame ~0.36ms (medido F4.9, 32 triángulos, 128×128, threaded=false)
```

> F4.9 medido en preset release (Ryzen 5 7600, clang 20): triángulo 128×128 con culling
> activo y color interpolado; pixel = present sRGB ya incluido. Baselines a refinar cuando
> exista la pipeline de cámara (F4.10).

### Volumen estimado (C++)

```
F0:        ~500 líneas    (harness + layout + reglas)
F1:        ~4.000         (math + tests + benchs)
F2:        ~3.500         (core + tests)
F3:        ~2.500         (platform)
F4:        ~5.000         (renderer + tests)
F5:        ~3.000         (ECS + tests)
F6:        ~2.000         (runtime + main + CI)
F7+F8:     ~8.000         (IA + Blueprint)
F9-F12:    ~45.000        (assets, streaming, escena, audio, editor, contenido)
F13-F14:   ~80.000        (optimización + renderer-class, multiplataforma)
F15:       ~15.000        (online: netcode, partición, telemetría, seguridad)
```

---

## 8. Riesgos y Mitigaciones

| Riesgo | Mitigación |
|---|---|
| C++ complejidad/UB | `-fno-exceptions`, `std::expected`, ASan/UBSan desde F0, Werror |
| Scope creep | Fases con acceptance criteria explícitos |
| IA/Blueprint demasiado pronto | Skeletons hasta F7/F8 — el core se estabiliza primero |
| Tiempos de compilación | ccache, Ninja, boundaries de módulos, unity builds si hace falta |
| Paridad de rendimiento rota | Gates de benchmarks — regresión = bug, no se avanza |
| Deuda de estilo | clang-format + clang-tidy en CI — la máquina hace cumplir el estilo |
| Rutas de error sin testear | Fault injection (ADR-016) + property tests (ADR-017) desde F2 |
| "Works on my machine" | Toolchain hermético (ADR-025): devcontainer, compiladores pinnados, CI reproducible |
| Bugs que no se reproducen | Crash pipeline (ADR-015) + determinismo/replay (ADR-013) |
| Saves/assets que explotan | Schema versioning + migraciones (ADR-022), manifest de assets (ADR-021) |
| Docs que mienten | Docs generadas desde headers en CI (ADR-026) |
| IA descontrolada | AI control plane (ADR-027): manifest + permisos + audit log; código IA = PR (ADR-029) |
| IA regresiona silenciosa | AI eval harness (ADR-028): escenarios dorados en CI |
| Tests no pueden correr el engine | Headless mode (ADR-030): null backend, CI y sandbox de IA sin ventana |
| Simulación no reproducible | No hidden state (ADR-038) + determinismo/replay (ADR-013) |
| Color roto a futuro | Color management sRGB desde F4 (ADR-037) |
| Desarrollo a ciegas | Debug UI (ADR-035) + profiler jerárquico (ADR-036) desde F2/F6 |
| Referencias rotas por renombres | Assets por UUID (ADR-040): la identidad no es el path |
| IA sin visión del mundo | Render targets (ADR-041) + context curation (ADR-044) |
| Contenido externo peligroso | Content trust boundary (ADR-042): sandbox para mods/contenido IA |
| Sin trazabilidad release → crash | Build ID estampado + semver + changelog (ADR-049) |
| Regresión de rendimiento invisible | Perf dashboard en CI (ADR-047): curvas por métrica |
| Sin undo/redo ni auditoría | Mutaciones por comandos (ADR-039): editor + IA + replay en una pieza |
| Reconstruir la rueda (física, audio, formatos) | Dependencias vendored tras interfaces propias (ADR-050/061): se integra, no se reinventa |
| Acoplamiento a una lib específica | Cada dependencia detrás de interfaz propia (ADR-061); reemplazo = cambiar backend, no callers |
| Deuda legal por licencias | Auditoría de licencias en CI (ADR-068): licencia/provenance/why; incompatibilidad = CI rojo |
| Editor duplicado | El editor ES el engine (ADR-064): una arquitectura, un loop, un mundo |
| IA monolito que no escala | Multi-agent con especialistas (ADR-066) + prompts como data versionada (ADR-065) |
| Save/load incompatibles | Save = escenario (ADR-063): un solo formato, cargar = replay |
| API quebradiza | API stability policy (ADR-053): semver, deprecación con ventana, breaking solo major |
| ABI de plugins frágil | Gameplay como plugins (ADR-054): boundary explícito y ABI estable desde F0 |
| IA que no aprende del proyecto | Memory store interno (ADR-057): la IA arranca con contexto curado, no en blanco |
| NaN/Inf rompe el determinismo | Math determinista (ADR-056): sin -ffast-math, política explícita, portabilidad testeada |
| Mundo que no escala a MMO | Partición por zonas/shards + persistencia first-class desde el diseño (ADR-072/073/083): los límites se declaran, no se descubren |
| Procgen que genera mundos distintos | Seed como contrato (ADR-076): mismo seed → mismo mundo, testeado como invariante |
| Renderer que no llega a Nanite/Lumen | Targets GPU-driven/virtualizado/GI (ADR-079/080/081) condicionan las decisiones desde F4, no se agregan al final |
| Shader compile hitching | Materiales y shaders como data (ADR-089/090): compilación offline + PSO caching, cero trabajo en runtime |
| Strings imposibles de localizar | i18n desde día 1 (ADR-091): ningún string hardcodeado en engine ni gameplay |
| Balance/ajuste enterrado en código | Tuning data-driven (ADR-092): el balance vive en data versionada, no en recompilaciones |
| La red miente en los tests | Red simulada en eval (ADR-074): latencia/pérdida/jitter reales desde CI |
| Bandwidth fuera de control | Bandwidth budgets por sistema (ADR-075): como el frame budget, superarlo es bug |
| Streaming que corta el mundo | World streaming por chunks (ADR-077) con prioridad y presupuesto de IO |
| Replicación desperdiciada | Interest management (ADR-093): el server replica solo lo relevante por jugador |
| Seguridad online rota | Client = terminal no confiable, server authority (ADR-096/062): el server valida y manda |
| Compilación que explota con el crecimiento | Compilación a escala (ADR-097): tiempo de build como métrica de CI con presupuesto |
| Mods que rompen el engine | Modding = tres caminos (ADR-095): plugins nativos, data/cooked e IA — sin scripting nuevo |

---

## 9. Próximos Pasos Inmediatos

1. ✅ Decisiones confirmadas (sección 4)
2. ✅ Docs recreadas (VISION, ARCHITECTURE, ROADMAP, rules/)
3. ⏳ F0.1 — Layout CMake + presets
4. ⏳ F0.2-F0.4 — Toolchain, doctest vendored, sanitizers
5. ⏳ F0.5-F0.7 — CI + format/lint + verificación del harness opencode
6. ✅ F1 — Math Core

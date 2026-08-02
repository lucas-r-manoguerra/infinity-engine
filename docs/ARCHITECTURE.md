# Infinity Engine — Arquitectura

> Documentación arquitectónica del Infinity Engine (C++).
> Este archivo crece con el proyecto. Refleja las decisiones actuales.
> Las fases F0–F15 se definen en `docs/ROADMAP.md` §5 (tabla de fases).

---

## 📁 Estructura de Directorios

Layout estándar de la industria para proyectos C++ escalables:
**librerías por módulo, ejecutables separados, tests espejo, dependencias vendored.**

```
Infinity-Engine/
├── CMakeLists.txt            # Root — targets agregados por módulo
├── CMakePresets.json         # Presets: debug / release / ci
├── cmake/                    # Toolchain, helpers, opciones del build
├── engine/                   # El engine — CADA módulo = static library
│   ├── core/                 # Base: Allocator, Time, Loop, Diagnostics, ThreadPool
│   ├── math/                 # Vec2/3/4, Mat4, Quat, Transform
│   ├── platform/             # Window, Input + backends (headless hoy; X11/Win32/Cocoa según disponibilidad)
│   ├── ecs/                  # World, Entity, Component, System, Query (F5, planificado)
│   ├── renderer/             # Interfaz + backends (software, vulkan)
│   ├── ai/                   # ContextSnapshot, Agent, Prompt, CodeGen (F7, planificado)
│   ├── blueprint/            # VM, Node, Graph, Compiler (→ C++) (F8, planificado)
│   └── runtime/              # Engine lifecycle: init → run → shutdown (F6, planificado)
├── apps/                     # Ejecutables
│   ├── sandbox/              # Entry point del engine (ventana + escena demo) (F6, planificado)
│   └── bench/                # Benchmarks (Release)
├── tests/                    # CTest — espejo de engine/
│   ├── core/  math/  ecs/  renderer/  ...
├── third_party/              # Dependencias vendored (solo doctest por ahora)
├── scripts/                  # Scripts de desarrollo (format, lint, hooks)
├── tools/                    # Codegen unificado (ADR-024): reflection, blueprint compiler, IA (F7, planificado)
├── docs/                     # VISION, ROADMAP, ARCHITECTURE, rules/
└── assets/                   # Contenido data-driven: source → cooked → load (F9, planificado; ADR-011)
```

### Estructura interna de un módulo

```
engine/math/
├── include/infinity/math/    # Headers públicos (un archivo = una tarea)
│   ├── vec3.h  mat4.h  quat.h  transform.h
├── src/                      # Implementación
│   ├── vec3.cpp  mat4.cpp  ...
└── CMakeLists.txt            # target: infinity_math
```

- **Público**: `engine/<mod>/include/infinity/<mod>/`
- **Privado**: `engine/<mod>/src/`
- **Tests**: `tests/<mod>/` → ejecutable `infinity_<mod>_tests` (doctest + CTest)

### Regla: "Un archivo = una tarea"

Cada archivo representa **exactamente UNA abstracción o responsabilidad**.

✅ **Bien**: `engine/math/include/infinity/math/vec3.h` — solo Vec3
✅ **Bien**: `engine/ecs/src/query.cpp` — solo la iteración de queries
❌ **Mal**: `engine/math/include/infinity/math/math_all.h` — todo junto
❌ **Mal**: `engine/ecs/src/world.cpp` con World + Entity + Component

Si un archivo pasa de ~300 líneas, es hora de partir.

---

## 🧩 Diagrama de Dependencias

```
apps/
  ├── bench   → infinity_math, infinity_renderer   (medición de hot paths desde F1)
  └── sandbox → infinity_runtime
engine/runtime  → core, platform, renderer, ecs
engine/renderer → core, math, ecs (+ platform con backend Vulkan, F4.5)   (los sistemas de render viven en el ECS, ADR-052/F5.12)
engine/ecs      → core, math
engine/platform → core
engine/ai       → core            (habla con el resto vía ContextSnapshot serializado)
engine/blueprint→ core            (genera C++ que linkea contra el resto)
engine/math     → (nada)
engine/core     → (nada)
```

### Reglas de Dependencia

1. **Los módulos de bajo nivel no conocen los de alto nivel**
   - `math/` no sabe que existe `ecs/` o `renderer/`
   - `core/` no sabe que existe `ai/` o `blueprint/`

2. **CMake las hace cumplir en link**: `infinity_renderer` linkea contra
   `infinity_math` pero `infinity_math` no puede linkear contra `infinity_renderer`.
   Una dependencia ilegal = error de build, no un debate.

3. **Plataforma siempre abstraída**
   - `platform/window.h` define la interfaz
   - `platform/src/headless/headless_window.cpp` implementa el backend actual (X11 pendiente)
   - El runtime nunca incluye headers del backend

4. **AI habla con todo a través de contexto serializado**
   - AI no importa ECS ni Renderer directamente
   - AI recibe un `ContextSnapshot` serializado (data-only)

---

## 🧠 Decisiones Arquitectónicas (ADRs)

### ADR-001: Migración a C++23 desde cero
**Contexto**: El engine nació en Zig. Por madurez de ecosistema, toolchains, sanitizers
y estándar de la industria AAA, se migra a C++.
**Decisión**: Re-implementación C++23 desde cero, con el layout de librerías descrito.
Sin rastro del código original.
**Consecuencia**: Código C++ idiomático desde el día 1. El roadmap (`docs/ROADMAP.md`)
define el orden de construcción módulo por módulo.

### ADR-002: C++23 (no C++20), headers clásicos
**Contexto**: C++23 está bien soportado en GCC 14+/Clang 20+/MSVC.
**Decisión**: `CMAKE_CXX_STANDARD 23`. Se usan `std::expected`, `std::flat_map/set`,
`deducing this`, `std::print`. **No** se usan modules (`import`) — el soporte
cross-compiler sigue inmaduro; compilación clásica header/source.
**Consecuencia**: Errores explícitos con `std::expected`, containers cache-friendly
para el ECS, sin riesgo de ABI de modules.

### ADR-003: `-fno-exceptions` + `std::expected`
**Contexto**: Las excepciones tienen costo impredecible en hot paths y los motores AAA
las evitan.
**Decisión**: Sin excepciones. Errores recuperables → `std::expected<T, E>`.
Errores de programación → `assert`/panic.
**Consecuencia**: Los containers STL que pueden fallar usan allocators explícitos;
nunca se depende de `bad_alloc` en hot paths. Error handling visible y verificable.

### ADR-004: Software Renderer para MVP
**Contexto**: Sin Vulkan SDK instalado. Necesitamos algo que renderice en pantalla desde el día 1.
**Decisión**: Renderer por software (framebuffer BGRA32 rasterizado por tiles, ADR-004)
sin dependencia de GPU; corre headless y vía el backend de plataforma (X11 pendiente).
**Consecuencia**: Iteración rápida sin depender de drivers GPU. Vulkan queda como backend futuro.

### ADR-005: Allocators Explícitos
**Contexto**: malloc global esconde el costo de memoria; los engines controlan su memoria.
**Decisión**: Cada subsistema recibe un `Allocator` explícito. El engine usa un arena
para allocaciones frame-scoped.
**Consecuencia**: Memoria predecible, zero GC pauses, fácil de trackear leaks (ASan).

### ADR-006: Fixed Timestep Game Loop
**Contexto**: El game loop determina cómo actualizamos lógica y renderizamos.
**Decisión**: Fixed timestep a 60Hz para updates, renderización desacoplada a máxima frecuencia.
**Consecuencia**: Física determinista. Misma simulación en diferentes framerates.

### ADR-007: ECS Archetype-based (futuro)
**Contexto**: Para el MVP usamos un ECS simple de sparse sets. Para AAA necesitamos archetypes.
**Decisión**: MVP con diseño simple pero con la misma interfaz. En F13 migramos a
archetypes sin cambiar la API pública.
**Consecuencia**: Podemos construir features arriba sin reescribir después.

### ADR-008: doctest vendored (primera dependencia)
**Contexto**: Un harness de tests propio es reinventar la rueda; el proyecto exige
dependencias solo vendored y auditadas, sin registry (ADR-061).
**Decisión**: `doctest` (single-header, MIT) vendored en `third_party/doctest/`.
No es un package manager ni una dep en runtime: es un header incluido en el repo.
**Consecuencia**: Tests con sections/BDD/parametrización desde el día 1, integrados
con CTest, sin costo de runtime.

### ADR-009: RHI — Render Hardware Interface
**Contexto**: El MVP renderiza por software (ADR-004), pero la visión incluye GPU
(Vulkan) a futuro. Si la escena, la cámara y el pipeline de assets escriben
directo contra el framebuffer, migrar a GPU es una reescritura total del renderer.
**Decisión**: El renderer se compone de dos capas: la **capa de alto nivel**
(escena, cámara, draw list, pipeline de assets — agnóstica del backend) y la
**RHI** (interfaz mínima: upload de buffers/texturas, submit de draw list,
present). El software renderer es **un backend más** detrás de esa interfaz,
exactamente igual que lo será Vulkan.
**Consecuencia**: La escena nunca sabe qué backend renderiza. Migrar a GPU =
escribir un backend nuevo, no reescribir el renderer. Los checksums de
framebuffer (regla 06) se testean contra el backend activo, con la misma draw list.

### ADR-010: Reflection + serialización genérica
**Contexto**: Un proyecto con editor, blueprints e IA necesita inspeccionar y
serializar componentes, campos y sistemas. Agregar esto tarde significa tocar
cada estructura del engine y mantener cientos de funciones escritas a mano.
**Decisión**: Un sistema de meta-datos (descriptores por componente: campos,
tipos, propiedades) + serialización genérica, diseñado **antes** de que existan
muchos componentes. Es el habilitador de editor visual, blueprints,
ContextSnapshot genérico, save/load, inspector y tooling.
**Consecuencia**: Costo de diseño temprano, retorno en cada feature futura.
Nuevos componentes se declaran con sus descriptores; editor/IA/blueprints los
leen sin conocimiento previo. Nunca se versionan serializadores a mano por componente.

### ADR-011: Asset pipeline + data-driven desde el día 1
**Contexto**: IA y blueprints necesitan un vocabulario de contenido (escenas,
prefabs, configs, blueprints). Si el contenido vive en código, no se puede
editar, versionar ni migrar; si el formato cambia tarde, hay que migrar cada asset.
**Decisión**: Todo el contenido es **data**, nunca código, con pipeline
`source → cooked → load`: formato fuente editable (JSON), formato cooked
eficiente para runtime, y un loader async. El directorio `assets/` y el sistema
de carga se diseñan desde F0; el contenido se puebla cuando haya features que lo
consuman.
**Consecuencia**: Blueprints e IA operan sobre data versionable. Un cambio de
formato es una migración del pipeline, no una reescritura de features.

### ADR-012: Modelo de threading formal desde F0
**Contexto**: El modelo de concurrencia es la decisión más cara de revertir en un
engine. Cambiarlo después de madurar = reescritura de arriba a abajo.
**Decisión**: Desde el día 1 se fijan los límites, aunque el MVP sea single-threaded:
- **Main thread**: simulación (world update).
- **Render thread**: submit de draw lists al backend (futuro).
- **Job pool**: trabajo paralelizable (física, IA, culling, load async).
- **Cero IO bloqueante en el main thread**: assets se cargan async desde el inicio.
Los accesos entre threads pasan por mecanismos explícitos (job handles,
framebuffers dobles, no locks globales escondidos).
**Consecuencia**: El código escribe "qué vive en cada hilo" desde el primer día.
Escalar a multi-threading real es activar workers, no rediseñar.

### ADR-013: Determinismo + replay
**Contexto**: Con IA nativa, un agente puede hacer algo inesperado en el frame
4312. Sin reproducción exacta, debuguear eso es adivinar. La simulación
determinista también habilita tests reproducibles y networking futuro.
**Decisión**: La simulación es determinista: RNG con seed controlado por frame,
fix timestep (ADR-006) como base, simulación separada de presentación. Se diseña
el **replay**: registrar inputs/eventos (no estados) para reproducir frames exactos
y usarlos como tests.
**Consecuencia**: Cualquier comportamiento (incluido el de IA) se reproduce y se
testea. El replay es la herramienta de debugging principal del engine.

### ADR-014: System registry declarativo
**Contexto**: Los engines mueren por init order frágil: un `init()` que solo
funciona si se llama en el orden exacto, con shutdown en el orden inverso, todo
a mano.
**Decisión**: Los sistemas se registran de forma declarativa (nombre, dependencias,
orden de init/shutdown resuelto por el registro, no por el programador). El
runtime resuelve el orden de arranque y parada a partir de las dependencias.
**Consecuencia**: Agregar/quitar sistemas no rompe el arranque. El orden real
queda documentado por construcción y verificable en tests.

### ADR-015: Crash pipeline — crash → repro → IA diagnóstica
**Contexto**: Con IA nativa, un agente puede hacer algo inesperado. Sin un
mecanismo que capture el fallo y permita reproducirlo, diagnosticar es adivinar.
**Decisión**: Ante crash o assert en release, el engine produce un **reporte
estructurado**: backtrace, captura de inputs/eventos del frame, snapshot del
mundo y contexto de la simulación. El reporte es consumible por el loop de IA
para auto-diagnóstico.
**Consecuencia**: La IA nativa recibe inputs de calidad para arreglar bugs. El
reporte se puede reproducir en tests (junto con ADR-013). El crash deja de ser
un misterio y pasa a ser un artefacto analizable.

### ADR-016: Fault injection + tests de rutas de error
**Contexto**: Con `std::expected` (ADR-003) todas las funciones declaran errores,
pero las rutas de error casi nunca se testean — es la falsa seguridad.
**Decisión**: Mecanismo de **inyección de fallos** controlado por tests: fail
alloc, fail IO, fail init, con probabilidad o punto de fallo explícito. Cada
rama de error del engine tiene al menos un test que la ejercita.
**Consecuencia**: Las rutas de error se prueban desde el día 1. Un error
declarado pero no testeado es un defecto conocido, no una deuda.

### ADR-017: Property-based tests + fuzzing en math y serialización
**Contexto**: Los test case-by-case dejan pasar edge cases (matrices degeneradas,
quats de 180°, round-trips de serialización).
**Decisión**: Property-based testing (invariantes con entradas aleatorias:
`inverse(M)*M == I`, quats unitarios, round-trip serialización == identidad)
+ fuzzing en los límites de math y serialización.
**Consecuencia**: Cobertura masiva de edge cases por casi cero esfuerzo. Las
invariantes quedan escritas como propiedades, no como ejemplos.

### ADR-018: System read/write sets
**Contexto**: El ADR-012 fija los hilos, pero el ECS necesita saber qué datos
toca cada sistema para poder paralelizar sin data races.
**Decisión**: Cada sistema declara explícitamente qué lee y qué escribe del
mundo (read/write sets). Hoy es documentación y validación; mañana es el input
del scheduler para ejecutar sistemas en paralelo.
**Consecuencia**: El paralelismo del ECS llega sin reescritura: el scheduler
solo necesita activar lo que ya está declarado. Los sistemas con sets
disjuntos se pueden ejecutar en paralelo de forma segura.

### ADR-019: Developer console + command system
**Contexto**: Debuguear un engine sin consola es manejar un avión sin panel.
Además, la IA necesita una forma de manejar el engine para probar sus
generaciones.
**Decisión**: Todo comando del engine (spawn, teleport, setvar, load scene) es
un **comando registrable**, invocable desde consola en ventana, socket o script.
El sistema de comandos es parte del core y se expone al runtime.
**Consecuencia**: Debug manual por teclado + la IA maneja el engine por
comandos para probar y verificar. Es la interfaz de control universal del
engine, útil también para tests de integración.

### ADR-020: Config unificada con precedencia explícita
**Contexto**: Los engines acumulan mecanismos de configuración ad-hoc (flags de
CLI, archivos, variables de entorno) que se pisan entre sí sin orden claro.
**Decisión**: Un solo sistema de configuración con precedencia explícita:
**consola > CLI > archivo > defaults**. Todo parámetro configurable pasa por
este sistema; nada de lecturas dispersas.
**Consecuencia**: "Qué flag gana acá" nunca es una pregunta. La configuración
es inspeccionable y testeable como un todo.

### ADR-021: Hot reload de assets + manifest
**Contexto**: Iterar contenido (texturas, meshes, blueprints) reiniciando el
engine es lento; para el loop de IA es intolerable.
**Decisión**: El pipeline de assets (ADR-011) incluye **manifest con hashes y
dependencias** entre assets, y hot reload: al cambiar un archivo fuente, el
asset se recocina y se recarga sin reiniciar.
**Consecuencia**: Iteración de contenido en segundos. Las dependencias entre
assets se conocen por el manifest, habilitando recargas en cadena y detección
de assets huérfanos.

### ADR-022: Schema versioning de assets/saves con migraciones
**Contexto**: El contenido y los saves evolucionan. Sin versiones, un save viejo
explota o corrompe.
**Decisión**: Todo formato de data (asset, save, blueprint) lleva **versión de
schema + migración explícita**. Un cambio de formato agrega una migración, no
una ruptura.
**Consecuencia**: Los saves de ayer abren mañana. La migración es parte del
pipeline (ADR-011), testeable y versionable como el código.

### ADR-023: Abstracción FS/threads/time + UTF-8
**Contexto**: La plataforma está abstraída para window/input, pero FS, threads y
tiempo también dependen del SO. Sin abstracción, multiplataforma (F14) es una
reescritura.
**Decisión**: `core` expone interfaces agnósticas de **filesystem, threads y
time** (con implementaciones por SO), y **UTF-8 como única codificación** en
todo el engine.
**Consecuencia**: Windows/Mac llegan sin dolor. Los paths y strings no generan
bugs de encoding. El core no sabe qué SO corre.

### ADR-024: Pipeline de codegen unificado (`tools/`)
**Contexto**: Reflection (ADR-010), compilador de blueprints y codegen de IA son
tres generadores de código que compartirán infraestructura. Sin un pipeline
común, divergen y se pelean.
**Decisión**: Un pipeline de generación unificado en `tools/` (parseo →
registro → generación → revisión). El output de cualquier generador se revisa
como código, se testea como código y pasa los mismos gates (regla 09).
**Consecuencia**: Los tres generadores comparten infraestructura y convenciones.
El código generado por IA y por blueprints es indistinguible en calidad y
revisión del código humano.

### ADR-025: Toolchain hermético/pinned
**Contexto**: "Works on my machine" es el enemigo #1 de los proyectos colosales.
Versiones de compilador y deps que flotan rompen builds a mitad de camino.
**Decisión**: Toolchain **fijado y reproducible**: compiladores pinnados
(GCC 14+/Clang 20+ exactos), presets CMake versionados, devcontainer o Docker
con el entorno completo, CI reproducible desde el mismo entorno.
**Consecuencia**: El build es el mismo en cualquier máquina y en CI. Los bugs
de "versión de compilador" desaparecen.

### ADR-026: Docs generadas desde headers en CI
**Contexto**: Las docs se desincronizan del código; los engines terminan con
documentación que miente.
**Decisión**: Los headers públicos llevan Doxygen; la CI **genera la doc y
falla si está desincronizada** con los headers. Las guías de alto nivel viven
en `docs/` (source of truth), la doc de API se genera.
**Consecuencia**: La documentación de API nunca miente. La documentación de
decisiones y guías vive en el repo y se revisa en PR.

### ADR-027: AI Control Plane — capability manifest + permisos + audit log
**Contexto**: La IA nativa podrá ejecutar comandos, generar código y modificar
assets. Sin un contrato explícito de qué puede hacer, es un empleado sin ficha.
**Decisión**: El engine expone a la IA un **control plane**: un manifest
versionado de capacidades (qué acciones puede tomar), niveles de permiso
(qué requiere aprobación humana) y un **audit log inmutable** de toda acción.
**Consecuencia**: La IA es auditable y controlable: se sabe qué hizo, cuándo y
quién lo autorizó. Las capacidades se versionan y se revisan como API pública.

### ADR-028: AI Eval Harness — la IA también se testea
**Contexto**: Si la IA genera código y respuestas, una regresión en prompts o
en el modelo no se detecta hasta que algo explota.
**Decisión**: **Escenarios dorados** con veredicto automático ("dada esta
escena, generá X") como suite de tests del agente. Cada cambio en prompts,
modelo o generación corre la suite.
**Consecuencia**: La IA se testea como código: una regresión en el comportamiento
del agente se detecta en CI, no en producción. Actualizar el modelo es un cambio
revisable, no una ruleta.

### ADR-029: El código generado por IA es un PR, no un commit
**Contexto**: El codegen de IA (ADR-024) produce código que debe ser confiable.
Un commit directo de la IA rompe la confianza.
**Decisión**: Todo código generado por IA pasa el **mismo pipeline de revisión
que el humano**: compila, corre tests, pasa los gates de la regla 09, y recién
con todo verde se integra — con rollback automático y canary si toca rama
principal.
**Consecuencia**: La IA no puede romper el repo: su código entra como un PR
revisable y se revierte solo si falla. El riesgo de la IA se convierte en
riesgo normal de PR.

### ADR-030: Headless mode (null backend)
**Contexto**: Los tests de integración y las validaciones de la IA necesitan
correr el engine completo, pero abrir una ventana en CI o en un sandbox no es
viable.
**Decisión**: El RHI tiene un **backend null (no-op)**: el engine corre completo
sin ventana. La simulación, los sistemas y los comandos funcionan igual.
**Consecuencia**: CI corre el engine real (no tests sueltos). La IA ejecuta
escenarios headless para validar sus generaciones — su sandbox de pruebas es el
engine mismo, sin abrir una ventana.

### ADR-031: Event system como data
**Contexto**: Los sistemas necesitan comunicarse más allá de las queries ECS
("player_died", "item_picked_up"). Sin un canal explícito, el acoplamiento
directo crece.
**Decisión**: Un sistema de **eventos desacoplados, tipados y versionados**
como data. Los eventos son first-class: se serializan, se versionan (ADR-022)
y se pueden observar por IA, blueprints y tooling.
**Consecuencia**: IA y blueprints reaccionan al mundo a través de eventos, no
acoplándose a sistemas. El flujo del juego es inspeccionable y versionable.

### ADR-032: Prefabs — plantillas de composición data-driven
**Contexto**: Crear entidades por código ("enemigo = mesh + ai + health + drop")
no escala y no es editable.
**Decisión**: **Prefabs**: plantillas data-driven de composición de entidades
(componentes + valores + referencias a assets). El prefab es la unidad que
edita el editor y la que instancia el runtime.
**Consecuencia**: El contenido se compone, no se programa. El puente entre
ECS, asset pipeline y blueprints queda definido; el editor edita prefabs, no código.

### ADR-033: Input completo — gamepad, rebinding, cola determinista
**Contexto**: F3 promete bindings configurables, pero input se vuelve un
infierno si llega tarde (gamepad, rebinding, reproducción determinista).
**Decisión**: **Input como data**: actions → bindings, rebindeable en runtime,
con soporte gamepad/joystick desde el día 1, y **cola de input determinista**
(el mismo input grabado reproduce la misma simulación — ADR-013).
**Consecuencia**: El input es configurable por el usuario y por la IA, y el
replay captura input, no estados (consistente con ADR-013/015).

### ADR-034: Memory budgets por subsistema
**Contexto**: Con allocators explícitos (ADR-005) el mecanismo existe, pero sin
política de límites el motor se entera tarde de que se quedó sin memoria.
**Decisión**: Cada subsistema declara su **presupuesto de memoria**; el engine
lo trackea y alerta al superarlo (log + dev console + tests).
**Consecuencia**: Los límites de memoria son visibles desde el día 1. Un
subsistema que crece sin límite se detecta temprano, no en release.

### ADR-035: Debug UI immediate-mode (custom, cero deps)
**Contexto**: Sin overlay de debug (inspector de ECS, visualización de físicas,
consola en ventana), se desarrolla a ciegas.
**Decisión**: **Debug UI immediate-mode** escrita a mano (consistente con cero
dependencias): overlays, inspector, visualización, consola renderizada. Es la
base del futuro editor (F12).
**Consecuencia**: Instrumentación visual desde F6. El editor futuro reutiliza
el mismo sistema en vez de nacer de cero.

### ADR-036: Profiler jerárquico + frame capture desde F0
**Contexto**: F2 promete contadores, pero instrumentar todo a posteriori es
doloroso y lento.
**Decisión**: **Spans jerárquicos** con costo en el frame (sin allocaciones) y
**captura de frame** para el renderer, diseñados desde F0. El profiler es la
primera feature del editor (F12).
**Consecuencia**: Cada sistema nace instrumentado. Medir una regresión de
rendimiento es un screenshot, no una tarde de profiling.

### ADR-037: Color management / sRGB correcto desde el primer píxel
**Contexto**: El color es notoriamente difícil de arreglar tarde: rehacer el
pipeline de color de todo el contenido es una migración visual dolorosa.
**Decisión**: El software renderer hace **color management correcto desde el
triángulo #1**: trabajo en linear, conversión linear → sRGB en presentación.
**Consecuencia**: Los colores son correctos desde el primer píxel. El contenido
se crea una vez; no hay migración de color a futuro.

### ADR-038: No hidden state (restricción de diseño)
**Contexto**: El determinismo (ADR-013) es la mitad del camino; la otra mitad
es que la simulación no guarde estado implícito (globals, contadores estáticos,
orden dependiente de punteros).
**Decisión**: **Restricción de diseño**: todo estado vive en el mundo o en
sistemas explícitos. Nada de estado implícito en la simulación. Se verifica con
tests (replay dos veces → mismo resultado).
**Consecuencia**: Habilita networking futuro (rollback netcode), replay exacto
y tests deterministas por construcción. El estado de la simulación es
serializable por diseño.

### ADR-039: Toda mutación del mundo pasa por comandos
**Contexto**: Si los sistemas mutan el mundo directamente (spawn, mover,
destruir), no hay undo/redo, no hay audit, y el replay (ADR-013) no tiene
dónde engancharse.
**Decisión**: **Toda mutación del mundo es un comando** — no un método
invocado directamente. Los comandos son data (tipados, versionados), se graban
en el audit log (ADR-027) y alimentan el replay (ADR-013).
**Consecuencia**: Una sola unidad de cambio conecta **editor (undo/redo) +
IA (audit) + determinismo (replay) + tooling (inspección)**. El mundo no se
mutó sin dejar rastro — nunca.

### ADR-040: Identidad de assets por UUID, no por path
**Contexto**: Renombrar un archivo rompe referencias si la identidad es el
path. En un proyecto colosal, referencias rotas = días perdidos.
**Decisión**: Cada asset tiene un **UUID estable**; el path es solo su
ubicación actual. El manifest (ADR-021) mapea UUID → archivo. Las referencias
apuntan a UUID, nunca a path.
**Consecuencia**: Renombrar/mover archivos no rompe nada. El editor y la IA
referencian assets por identidad estable, y el manifest detecta huérfanos.

### ADR-041: Render targets (offscreen) desde el día 1
**Contexto**: Un renderer que solo dibuja a la ventana no puede sostener
editor, minimaps, debug UI ni IA visual.
**Decisión**: El RHI (ADR-009) soporta **render targets** (dibujar a texturas,
no solo a la ventana) desde F4. La presentación a pantalla es un render
target más.
**Consecuencia**: El editor (F12) tiene viewports, la debug UI (ADR-035) se
compone, y **la IA puede "ver" el juego** renderizando a un buffer que consume
como input (multimodal futuro).

### ADR-042: Content trust boundary — los blueprints son código
**Contexto**: Un blueprint es código ejecutable, y va a llegar de contenido de
usuarios y de la IA. Ejecutar contenido no confiable sin barrera es un riesgo.
**Decisión**: **Content trust boundary**: el contenido del proyecto es de
confianza; el contenido externo (mods, generado por IA sin aprobar) se ejecuta
validado o sandboxeado (validación de tipos, límites de recursos, permisos).
**Consecuencia**: El día que haya mods o contenido generado, la seguridad ya
está diseñada. La IA (ADR-027) opera con los permisos del control plane.

### ADR-043: glTF como formato de intercambio
**Contexto**: Importar contenido externo sin un formato estándar termina en
"50 formatos de mesh" crónicos.
**Decisión**: **glTF** (Khronos, textual, extensible) es el formato de
intercambio para meshes/escenas. Se escribe un importador propio (cero deps,
filosofía del proyecto) y el cooked format (ADR-011) es lo que corre.
**Consecuencia**: Una sola puerta de entrada para contenido 3D. El pipeline
de assets tiene un formato de origen estándar y un formato runtime propio.

### ADR-044: Context curation — la IA ve lo que le conviene
**Contexto**: `ContextSnapshot` (ADR-044) puede explotar: la IA no puede ver
el mundo entero, y todo el contexto cuesta tokens y atención.
**Decisión**: El snapshot pasa por **curaduría de contexto**: qué entidades,
qué eventos, qué estado — priorizado, limitado y resumido por **presupuesto de
tokens** explícito.
**Consecuencia**: La IA entiende la escena en vez de ahogarse en datos. El
presupuesto de contexto se configura y se mide, igual que la memoria (ADR-034)
y el rendimiento (ADR-055).

### ADR-045: Hot reload de código nativo (dev-only)
**Contexto**: En el loop de IA que genera C++ y lo testea, reiniciar el engine
en cada iteración es intolerablemente lento.
**Decisión**: **Hot reload de código nativo** en dev: rebuild → reload de la
shared lib → el engine sigue corriendo. Dev-only, afecta cómo se estructuran
los módulos de gameplay (boundaries dinámicos).
**Consecuencia**: Iteración de código en segundos, no en minutos. La IA prueba
sus generaciones en el engine vivo. El build de release no paga este costo.

### ADR-046: Structured logging como data
**Contexto**: El logging por `printf` no escala: no hay niveles, canales,
ni sinks; el crash pipeline (ADR-015) necesita contexto estructurado.
**Decisión**: El logging es **data**: niveles, canales por subsistema, sinks
(archivo, socket, consola, reporte de crash). Cero `printf` en producción.
**Consecuencia**: El engine se observa como sistema: el profiler, la
telemetría y el debugging de IA consumen logs estructurados. El reporte de
crash (ADR-015) incluye el log del frame.

### ADR-047: Perf regression dashboard en CI
**Contexto**: Los gates de benchmark por PR (regla 08) no detectan regresiones
lentas que pasan un punto de ruido.
**Decisión**: Los benchmarks se **trackean en el tiempo**: curva por métrica
en CI, comparación contra el baseline del mes.
**Consecuencia**: "Más lento que hace 3 semanas" se detecta solo, con la curva
como evidencia. Las regresiones graduales dejan de ser invisibles.

### ADR-048: Modelo de tiempo extendido — determinista
**Contexto**: El loop (ADR-006) es fixed timestep, pero debuguear requiere
pause, slow-mo y time scale — sin romper el determinismo.
**Decisión**: El modelo de tiempo incluye **time scale, pause y slow-mo** como
parte de la simulación determinista (no como parches del loop). El replay
(ADR-013) controla el tiempo.
**Consecuencia**: Debug de física/IA en cámara lenta es nativo. El time scale
es un estado de la simulación, serializable y reproducible.

### ADR-049: Release automation — semver + changelog
**Contexto**: "¿Qué cambió entre esta build y la anterior?" no debe ser una
pregunta de dos horas en un proyecto colosal.
**Decisión**: Versión **semver** del engine, changelog **generado desde
conventional commits** (regla 10), tags automáticos, build ID estampado en los
binarios (crash pipeline ADR-015 lo necesita).
**Consecuencia**: Cada build sabe qué es y qué cambió. El changelog es contexto
para la IA y para el usuario. La trazabilidad crash → release es directa.

### ADR-050: Física vendored tras interfaz swappable
**Contexto**: La física es un problema colosal (broadphase, narrowphase, contacto,
solver) — reconstruirla desde cero es reinventar la rueda y arriesgar años de
trabajo para terminar peor que las librerías maduras. Pero acoplarse directo a
una lib específica condena al engine a su evolución para siempre.
**Decisión**: La física se implementa con una **librería vendored** en
`third_party/` detrás de una **interfaz de física propia** (patrón RHI,
ADR-009): el mundo habla con el contrato del engine, no con la lib. La elección
concreta de librería se decide al llegar a F10 con el proceso del ADR-061;
reemplazarla es cambiar un backend, no reescribir el engine. El MVP arranca
con el alcance mínimo que el backend elegido ofrezca y crece.
**Consecuencia**: El costo real de la física pasa de meses a semanas de
integración. El control está en el contrato, no en el detalle: el ECS (ADR-007)
con read/write sets (ADR-018) acomoda la física como sistema, y el backend se
reemplaza con costo mínimo (política ADR-061).

### ADR-051: Cámaras first-class
**Contexto**: La cámara no es un "jugador con posición": es un sistema (principal,
minimap, editor, IA observadora). Diseñarla tarde = reescribir el renderer.
**Decisión**: **Sistema de cámaras first-class**: múltiples cámaras por escena,
cada una con render target (ADR-041) y capa de render propia. Se diseña con el
renderer (F4), no cuando "haga falta una cinemática".
**Consecuencia**: El editor (F12) y la IA visual nacen con el renderer, no como
parches. La cámara es data del mundo: serializable, determinista (ADR-038).

### ADR-052: El renderer es un sistema del ECS
**Contexto**: Si el renderer mira el mundo desde afuera, no hay un scheduler único
que paralelice render + gameplay (ADR-012) y las dependencias se oscurecen.
**Decisión**: Los sistemas de render (culling, submit) viven **dentro del ECS**
como sistemas con read/write sets (ADR-018), en el mismo job system. El RHI
(ADR-009) sigue siendo el backend puro y agnóstico.
**Consecuencia**: Paralelización real render + gameplay en el mismo scheduler;
la forma del sistema es explícita y testeable. "El renderer" deja de ser un
módulo que espía al mundo.

### ADR-053: API stability policy — deprecación, no ruptura
**Contexto**: Un engine que va a vivir décadas, con IA generando código contra él,
no puede romper la API en cada versión.
**Decisión**: **Política de API**: con semver (ADR-049), `minor` nunca rompe la
API pública. Deprecación con ventana de transición, breaking changes solo en
`major`, y una capa de compatibilidad testada.
**Consecuencia**: La IA y los usuarios generan código contra APIs estables;
la confianza a largo plazo es parte del producto. El costo es disciplina de
deprecación, y se paga en cada cambio de API.

### ADR-054: Gameplay como plugins (boundaries dinámicos)
**Contexto**: El hot reload nativo (ADR-045) implica gameplay compilado por
separado; además es la puerta a los mods (ADR-042) y al editor.
**Decisión**: El engine es el **host**; los módulos de gameplay son **plugins**
(shared libs, boundaries dinámicos). La API de plugin es la API pública del
engine (con la política ADR-053).
**Consecuencia**: Hot reload real, mods por diseño, y el editor hostea los mismos
módulos que el runtime. El costo: ABI estable y un boundary explícito diseñado
desde F0, no improvisado después.

### ADR-055: Time budgets por sistema
**Contexto**: El frame budget global (regla 08) no dice qué sistema se comió el
presupuesto.
**Decisión**: Cada sistema declara su **ms/frame máximo**; el profiler (ADR-036)
lo verifica, el dashboard (ADR-047) lo grafica, y superarlo es un bug visible.
**Consecuencia**: Las discusiones de rendimiento pasan a ser por datos por
sistema, no opiniones por frame. El presupuesto es parte de la definición de
cada sistema.

### ADR-056: Math determinista — sin `-ffast-math`, política NaN/Inf
**Contexto**: Un engine con determinismo (ADR-013) y networking futuro no puede
tener math "undefined, whatever". F1 es el momento barato de decidirlo.
**Decisión**: **Sin `-ffast-math`** (rompe determinismo y exactitud IEEE);
política explícita de NaN/Inf y división por cero; los mismos resultados en
todas las plataformas.
**Consecuencia**: Determinismo portable de verdad. La math es testeable contra
los baselines (mat4.mul ~34ns, quat.slerp ~75ns) y los invariantes sobreviven
al property testing (ADR-017).

### ADR-057: Memory store interno del engine (retrieval para IA)
**Contexto**: La IA necesita recordar: decisiones previas, dónde están las cosas,
qué se intentó. El snapshot (ADR-044) es estado del mundo; no es historia del
proyecto.
**Decisión**: Un **memory store interno del engine**: el engine guarda y recupera
contexto relevante (sesiones, decisiones de diseño, bugs conocidos) y lo inyecta
curado (ADR-044) al LLM.
**Consecuencia**: La IA aprende del proyecto en vez de arrancar de cero cada
sesión. Es infraestructura propia, no una lib de terceros, y el contenido que
maneja vive bajo el control plane (ADR-027).

### ADR-058: Scenarios como formato único
**Contexto**: Test de regresión, demo jugable y eval harness (ADR-028) hoy serían
tres sistemas de escenarios incompatibles.
**Decisión**: Un **escenario** (estado inicial + inputs + objetivo) es **data** y
sirve para los tres: test de regresión, demo jugable y escenario dorado del eval.
**Consecuencia**: Un solo formato, tres usos. El eval mide exactamente lo que
corre en producción, y la demo es un escenario que se juega.

### ADR-059: Packaging reproducible — el engine como producto
**Contexto**: El día que el engine se distribuya, empaquetar no debe ser un
proyecto paralelo.
**Decisión**: Pipeline de **packaging reproducible** desde F6: binario + assets
cooked (ADR-011) + runtime libs + licencias, con el mismo build hermético
(ADR-025).
**Consecuencia**: Distribuir es un script que ya existe, no una emergencia de
tres semanas. El producto instalable nace con el runtime, no después.

### ADR-060: Vertical slice como gate de fase
**Contexto**: "Un año de librerías y ninguna demo" es el riesgo clásico de un
engine.
**Decisión**: Cada fase (F1+) termina con un **demo corriendo**, no solo tests
verdes: F1 infinity-bench mide la math, F4 triángulo, F6 MVP. El motor
queda **siempre jugable**.
**Consecuencia**: Progreso visible en cada hito y antídoto contra la espiral de
infraestructura. Cada fase se puede mostrar, y la IA valida (ADR-030) contra
demos reales.

### ADR-061: Dependency policy — usarlas sí, acoplarse nunca
**Contexto**: "Cero dependencias" como dogma es un error: hay problemas colosales
(física, audio, formatos, UI) donde reconstruir es reinventar la rueda y pagar
años. Pero acoplar el engine a una lib específica es una deuda que se paga
para siempre. **CORRIGE la filosofía previa y el ADR-050 original.**
**Decisión**: **Política de dependencias**:
1. Se usa una dependencia cuando el costo de reconstruirla es colosal y el
   problema no es el core del engine.
2. Toda dependencia se **vendorea** en `third_party/` con provenance y licencia
   auditada (ADR-068) — sin package managers.
3. Toda dependencia se aísla detrás de una **interfaz propia** del engine
   (patrón RHI, ADR-009): el resto del código no conoce la lib.
4. Reemplazar una dependencia debe ser cambiar un backend, no reescribir
   callers: se exige por diseño, no como promesa.
**Consecuencia**: El engine usa las mejores ruedas del mercado sin casarse con
ninguna; el costo de reemplazo queda mínimo por construcción. "Sin dependencias"
deja de ser el norte; **"sin acoplamiento a dependencias"** lo es.

### ADR-062: Networking como restricción de diseño, no fase
**Contexto**: El determinismo (ADR-013/038) habilita rollback netcode, pero solo
si el gameplay se diseña con inputs/comandos como unidad de red desde el inicio.
Decidir multiplayer después = rediseñar el gameplay.
**Decisión**: **Restricción de diseño**: la API de gameplay se diseña asumiendo
que mañana es online — y el alcance cubre juegos **online de entrada** (MMO,
RPG, MMORPG). Esto significa:
1. **Server authority**: el server es la verdad del mundo; los clients predicen
   y rebobinan. El server dedicado es un **modo del mismo binario** (headless,
   ADR-030), no un proyecto aparte.
2. Inputs y comandos (ADR-039) son la unidad de red; el mundo se sincroniza por
   estado + inputs.
3. El mundo está pensado para **particionarse** (zonas/shards) y **persistir**
   (save/load ADR-063 extendido al server): MMO-scale no es un mundo gigante
   único e inseparable.
No retrasa el MVP: es una restricción de forma, no una feature.
**Consecuencia**: El netcode llega sin reescribir gameplay. Los comandos ya son
replay-friendly (ADR-039) y el estado es serializable (ADR-038); la red y el
server son otros consumidores del mismo diseño. Las decisiones de escala
(partición, persistencia, streaming) se toman como forma del mundo, no como
parche.

### ADR-063: Save/load es un escenario
**Contexto**: Guardar el mundo con un formato ad-hoc paralelo al resto de los
formatos = dos sistemas que versionar y migrar (ADR-022) por separado.
**Decisión**: **Un save ES un escenario (ADR-058)**: estado completo serializado
(reflection ADR-010) + cola de comandos (ADR-039). Cargar = replay hasta el
frame actual. Cero formatos paralelos.
**Consecuencia**: Un formato menos, y el load queda verificado por el mismo
machinery del determinismo. Guardar/cargar es un caso del problema ya resuelto.

### ADR-064: El editor ES el engine
**Contexto**: Un editor que controla un engine externo duplica arquitectura:
dos apps, dos loops, dos mundos que mantener.
**Decisión**: **El editor es el mismo engine con herramientas**: hostea gameplay
como plugins (ADR-054), pausa con tiempo extendido (ADR-048), edita con
comandos (ADR-039) y ve por cámaras (ADR-051). No existe "runtime vs editor"
como mundos distintos.
**Consecuencia**: Todo lo que se construye para el runtime sirve al editor y
viceversa. El editor nace como un modo del engine (F12), no como una segunda
arquitectura.

### ADR-065: El prompt de la IA es data versionada
**Contexto**: Prompts como strings mágicos en el código no se pueden testear,
diffear ni versionar; una IA que falla por un prompt no se debuguea.
**Decisión**: Los prompts y el contexto de la IA son **data versionada**
(schema ADR-022) que vive en el repo, se testea en el eval harness (ADR-028) y
evoluciona por PRs como el código (ADR-029).
**Consecuencia**: La IA se debuguea como código: diffs de prompts, revisión y
regresión automática. Un cambio de prompt que rompe un escenario dorado = PR
rechazado.

### ADR-066: Multi-agent — especialistas coordinados
**Contexto**: Un agente monolito que hace todo no escala: contexto saturado
(ADR-044), foco disperso, errores que nadie revisa.
**Decisión**: **Arquitectura multi-agent**: especialistas con roles (diseñador,
implementador, revisor, debugger), cada uno con su presupuesto de contexto,
coordinados por un orquestador. El eval harness (ADR-028) evalúa al equipo.
**Consecuencia**: Cada agente hace una cosa bien, y el revisor es distinto del
autor — los errores se ven. El orquestador tiene el mapa; los especialistas,
el detalle.

### ADR-067: Content packs — los creadores no tocan el repo
**Contexto**: Si artistas y diseñadores editan dentro del repo de código, cada
asset es un conflicto y el pipeline se contamina.
**Decisión**: El contenido vive en **content packs** (carpetas con manifest
ADR-021 + UUIDs ADR-040), unidades versionadas independientes del código. El
pipeline valida, cocina (ADR-011) y publica packs.
**Consecuencia**: Los creadores trabajan sin tocar el repo; el día de los mods
(ADR-042) el pipeline ya existe. Contenido y código evolucionan a ritmos
distintos sin pisarse.

### ADR-068: Licencias y provenance — auditoría en CI
**Contexto**: Con dependencias permitidas (ADR-061), una lib con licencia
incompatible es deuda legal silenciosa que explota en release.
**Decisión**: **Auditoría de licencias en CI**: cada dependencia vendored
declara licencia, provenance y "why" (por qué se eligió). CI falla si una
licencia es incompatible o falta la declaración.
**Consecuencia**: La deuda legal no entra al repo. La decisión de cada
dependencia queda documentada para el futuro y para la IA (ADR-069).

### ADR-069: El changelog es input de la IA
**Contexto**: El changelog (ADR-049) es la historia del proyecto; si solo lo
leen humanos, la IA ignora qué cambió, qué se rompió y qué está deprecado.
**Decisión**: El changelog es **data que la IA consume**: el memory store
(ADR-057) lo indexa; los agentes lo leen antes de tocar APIs (ADR-053) para no
reintroducir lo deprecado.
**Consecuencia**: Un changelog mal escrito = una IA que no entiende la historia.
La disciplina del changelog pasa a ser parte del harness, no un trámite.

### ADR-070: Codegen y reflection comparten el modelo del código
**Contexto**: Reflection (ADR-010), blueprint compiler y la IA que genera C++
comparten el mismo problema — parsear, registrar, generar. Si cada uno tiene su
tooling, hay tres gramáticas que divergen.
**Decisión**: **Un solo pipeline de codegen (ADR-024)** con un modelo común del
código (AST + registro): reflection, blueprints e IA generan y hablan el mismo
idioma.
**Consecuencia**: Una gramática, un registro, cero forks de tooling. Una mejora
del codegen beneficia a los tres consumidores a la vez.

### ADR-071: Internal dev tools son first-class
**Contexto**: Un script roto del repo cuesta más que un bug de runtime: lo paga
cada developer cada día, y la IA que ejecuta comandos lo paga doble.
**Decisión**: Las herramientas internas (`scripts/`, `tools/`) son **producto**:
se documentan, se testean y tienen dueño. Si algo se usa dos veces, vive en
`tools/` con tests.
**Consecuencia**: El harness no se pudre. La regla "se usa dos veces → tools/
con tests" mantiene el repo confiable para humanos e IA.

### ADR-072: Persistencia como sistema first-class
**Contexto**: En un juego online (ADR-062), la data de estado (jugadores,
inventarios, mundo, economía) no es "guardar partidas" — es un servicio del
server con versiones, migraciones y lecturas concurrentes.
**Decisión**: La persistencia es un **sistema first-class** del engine:
serialización (ADR-010) + schema versioning (ADR-022) + el save/load = escenario
(ADR-063) extendido al server. El mundo persistido ES un escenario enorme;
cargar = replay.
**Consecuencia**: La persistencia se diseña con el ECS y la serialización desde
F5/F6, no cuando "haya servidores". Un MMO con saves ad-hoc es deuda que se
paga en cada release.

### ADR-073: El mundo se particiona — zonas/shards como estructura
**Contexto**: Un MMO no es un mundo gigante único: es una colección de
simulaciones (zonas) con jugadores que viajan entre ellas. Un World único e
inseparable no escala.
**Decisión**: **La partición del mundo es parte del diseño del ECS**: zonas
como unidades de simulación, portales/transiciones como data, cada zona con su
propio determinismo (ADR-013). La zona es la unidad de simulación, memoria y red.
**Consecuencia**: Escalar = agregar zonas; el server dedica procesos a zonas
calientes. Los clients solo cargan la zona visible (conecta con streaming
ADR-077).

### ADR-074: Condiciones de red simuladas en el eval
**Contexto**: El netcode testado sin red real se rompe en producción con lag,
jitter y pérdida de paquetes.
**Decisión**: Las condiciones de red (latencia, jitter, pérdida) son
**parámetros de escenario** (ADR-058): el eval harness (ADR-028) ejecuta el
mismo partido con distintas condiciones y mide consistencia y jugabilidad.
**Consecuencia**: "El mismo partido con 50ms vs 250ms" es un test reproducible,
no una anécdota. El determinismo (ADR-013) hace medible el daño de la red.

### ADR-075: Bandwidth budgets declarados
**Contexto**: En un MMO, bytes mal gastados = jugadores que se van. Sin
presupuesto, la replicación crece sin control.
**Decisión**: Cada entidad/sistema declara su **presupuesto de bytes/segundo**
de replicación (como memoria ADR-034 y tiempo ADR-055); superarlo es un bug
visible en el dashboard (ADR-047).
**Consecuencia**: El costo de red de un sistema se mide y se negocia, no se
descubre en el playtest con 500 jugadores.

### ADR-076: Procgen con seed como contrato
**Contexto**: La generación procedural que corre una vez y produce un mundo
arbitrario no es testeable ni reproducible — y en un online, cada client vería
algo distinto.
**Decisión**: El procgen es un **sistema del ECS** (ADR-018) cuya única entrada
aleatoria es la **seed** (ADR-013): misma seed = mismo mundo en todo client,
server y test.
**Consecuencia**: El procgen es parte de la simulación determinista: testeable
(property tests ADR-017), reproducible y auditable. La IA (ADR-078) genera
seeds y reglas, no caos.

### ADR-077: World streaming por chunks
**Contexto**: Mundo infinito/impar = memoria infinita si se carga todo. Sin
streaming, el mundo abierto es una demo de 1km².
**Decisión**: **Streaming asíncrono por chunks**: carga, cocina (ADR-011) y
recicla con memoria controlada (ADR-034) y presupuesto de tiempo (ADR-055). La
API de carga de contenido es asíncrona y observable desde el día 1.
**Consecuencia**: El mundo abierto escala en la memoria disponible, y el
streaming es la intersección natural de asset pipeline (F9), partición
(ADR-073) y presupuestos.

### ADR-078: IA + procgen comparten pipeline de generación
**Contexto**: Si la IA genera contenido con un pipeline y el procgen con otro,
hay dos mundos que no se hablan.
**Decisión**: La IA (F7) y el procgen generan **lo mismo con distinto motor**:
la IA produce reglas y seeds (data), el procgen las ejecuta determinísticamente
(ADR-076), el artista cura el resultado (content packs ADR-067).
**Consecuencia**: "Generá un bioma pantanoso" es la IA escribiendo una regla
que el engine ejecuta — un solo pipeline de generación de contenido (ADR-070),
no dos herramientas.

### ADR-079: GPU-driven rendering como target
**Contexto**: Un renderer con una draw call por objeto y estado atómico no
escala a Nanite-class; la representación de escena decide si la GPU puede cull
y dibujar sola.
**Decisión**: **Target**: GPU-driven rendering (bindless, indirect draws,
culling en GPU). La decisión de HOY: la representación de escena (ADR-052) se
diseña como **data estructurada que la GPU puede consumir**, no como llamadas
imperativas.
**Consecuencia**: El software renderer (F4) dibuja en el mismo formato que un
día dibujará la GPU. El target no es trabajo de F4 — la forma sí.

### ADR-080: Geometría virtualizada (Nanite-like) como target
**Contexto**: Millones de triángulos visibles requieren LOD virtualizado; un
mesh asset de un solo nivel no lo permite.
**Decisión**: **Target**: mallas con LOD jerárquico virtualizado y streaming.
La decisión de HOY: el **formato de mesh** (F9) no asume vértices fijos —
asume una jerarquía de LODs; el importador glTF (ADR-043) cocina para eso.
**Consecuencia**: El formato de mesh nace para el target, no se reescribe
cuando el target llegue. F4 renderiza mallas simples del mismo formato.

### ADR-081: Global illumination (Lumen-like) como target
**Contexto**: La iluminación global dinámica se decide tarde o nunca; cambiarla
después = reescribir shaders y materiales.
**Decisión**: **Target**: GI dinámica (no lightmaps horneados). La decisión de
HOY: el **modelo de iluminación** (forward+/deferred/clustered) se elige con
GI en mente; color management sRGB (ADR-037) ya es requisito desde F4.
**Consecuencia**: El pipeline de materiales (ADR-089) y el lighting model no se
rehacen. GI se suma como un pase más, no como reescritura.

### ADR-082: Spatial partitioning como estructura core
**Contexto**: Broadphase de física (ADR-050), culling de render, streaming
(ADR-077) y zonas (ADR-073) necesitan la misma partición espacial — o cuatro
implementaciones divergentes.
**Decisión**: **Una estructura espacial** (grid jerárquico/octree) en core,
compartida: física, render, streaming y zonas usan el mismo tipo. La estructura
es parte de la capa core (cero deps internas).
**Consecuencia**: Un solo concepto de "dónde está cada cosa" en el engine; los
módulos hablan el mismo idioma espacial y comparten sus optimizaciones.

### ADR-083: La escala es un requisito de forma
**Contexto**: "Optimizamos después" es como llegan los sistemas que mueren a
los 10k entidades.
**Decisión**: Toda decisión de diseño se pregunta **"¿esto escala a 10k
entidades, 1k jugadores, 100km²?"** — la escala es un criterio de aceptación de
cada sistema, junto a los budgets (ADR-034/055/075).
**Consecuencia**: Los sistemas que no declaran su costo de escala no entran al
core. La optimización (F13) es ajuste, no rescate.

### ADR-084: Animación data-driven first-class
**Contexto**: RPG/MMO sin animación no existe; y un sistema de animación
spaghetti (clips en código) no sobrevive al primer personaje con IK.
**Decisión**: La animación es **data-driven**: skeletons, clips, blend trees y
state machines como data versionada (ADR-022), ejecutada por un sistema del
ECS — determinista (ADR-013) como toda la simulación.
**Consecuencia**: Los animadores editan data, no código; el editor (ADR-064)
la inspecciona; la IA (F7) puede generarla y el replay la reproduce igual.

### ADR-085: Gameplay AI como data (behavior trees, GOAP, nav)
**Contexto**: La IA de enemigos escrita a mano en cada juego no es reutilizable
ni testeable; el pathfinding ad-hoc no escala.
**Decisión**: El gameplay AI es **data versionada**: behavior trees/GOAP graphs
(data, como blueprints) + **navigation** (navmesh + pathfinding) como sistema
del ECS con read/write sets (ADR-018).
**Consecuencia**: La IA de juego se edita, se testea (property tests ADR-017
con seeds ADR-076) y la IA del engine (F7) la genera. Una base para todos los
juegos.

### ADR-086: UI de juego como sistema declarativo
**Contexto**: La debug UI (ADR-035) es immediate-mode para herramientas; la UI
de juego (menús, HUD, inventarios) necesita layout, data binding y
localización.
**Decisión**: **UI de juego** como sistema declarativo (widget tree + layout +
data binding), separada de la debug UI, renderizable headless (ADR-030) para
tests y en render targets (ADR-041).
**Consecuencia**: La UI es data: localizable (ADR-091), testeable en CI,
editable en el editor. El HUD se testea como cualquier sistema.

### ADR-087: Audio como sistema del ECS
**Contexto**: El audio es la mitad de la experiencia y suele ser un afterthought
pegado al código.
**Decisión**: El audio es un **sistema del ECS** (spatial 3D, streaming, mixer)
con presupuesto de voces/memoria (ADR-034), detrás de interfaz propia — la lib
de audio es vendored (ADR-061), reemplazable.
**Consecuencia**: Los eventos del mundo (ADR-031) disparan audio; el editor lo
inspecciona; y la política de dependencias aplica igual que con física.

### ADR-088: Terreno y mundo abierto
**Contexto**: Un mundo abierto sin sistema de terreno termina en "el suelo es
un mesh gigante" — la peor decisión de asset que existe.
**Decisión**: El terreno es **data streamable** (heightfield/geometry) con LOD
y vegetación **instanciada** (foliage), integrado con streaming (ADR-077) y
spatial partitioning (ADR-082).
**Consecuencia**: El mundo abierto se cocina, se streama y se optimiza como el
resto; el suelo no es un mesh estático de 4GB.

### ADR-089: Materiales y shaders como data
**Contexto**: Shaders compilados en runtime = hitching en producción;
materiales en código = artistas bloqueados.
**Decisión**: Los materiales son **data** (material graph compilado offline vía
codegen ADR-070), los shaders se compilan **offline** con PSO caching; cero
compilación en runtime en release.
**Consecuencia**: El shader compile en el playtest desaparece; los artistas
editan materiales en el editor (ADR-064) y el pipeline los cocina como assets
(ADR-011).

### ADR-090: Post-processing como pases data
**Contexto**: Bloom, DOF, tonemapping sin orden definido = arte corrupto en
cada release.
**Decisión**: El post-processing es una **secuencia fija de pases data**
(bloom, DOF, color, tonemap) con el color management (ADR-037) como última
palabra — el orden es parte del pipeline.
**Consecuencia**: El look es consistente entre builds; cada pase es data
versionada y configurable; el editor la ajusta en vivo.

### ADR-091: Localización e i18n desde el día 1
**Contexto**: Strings hardcodeadas = reescribir UI cuando llegue el primer
mercado no-inglés; fuentes sin fallback = CJK ilegible.
**Decisión**: Toda string de usuario es **data externa localizable** (schema
ADR-022) con font rendering y **fallback de fuentes** (CJK/RTL) desde F6; la
UI (ADR-086) la consume por clave.
**Consecuencia**: Traducir es agregar data, no tocar código. La IA puede
generar y revisar traducciones con el eval harness (ADR-028).

### ADR-092: Tuning data-driven (balance)
**Contexto**: El balance de un RPG/MMO cambia cada semana; balance en código =
hotfix en cada cambio de número.
**Decisión**: Las tablas de tuning (items, stats, spawns, economía) son **data
versionada** (ADR-022), ajustable en caliente en el server (hot reload
ADR-021), con la IA (F7) proponiendo ajustes sobre datos de telemetría
(ADR-094).
**Consecuencia**: El balance se itera sin releases; cada cambio queda versionado
y auditado — y el changelog (ADR-069) lo registra para la IA.

### ADR-093: Interest management — el server replica lo relevante
**Contexto**: Replicar todo el mundo a cada client no escala a 1k jugadores; el
cuello del netcode MMO es decidir QUÉ se manda a cada uno.
**Decisión**: **Interest management** como sistema del server: relevancy sets
(por proximidad, zona, visión), alimentados por spatial partitioning (ADR-082)
y presupuestos de bandwidth (ADR-075).
**Consecuencia**: Cada client recibe su mundo, no el universo. La regla es
declarativa y testeable con las condiciones de red simuladas (ADR-074).

### ADR-094: Telemetría y analytics como data
**Contexto**: Un online sin telemetría opera a ciegas; el crash pipeline
(ADR-015) y el logging (ADR-046) ya generan la data — falta el canal.
**Decisión**: La telemetría es **data estructurada** que el engine reporta
(eventos de juego, crashes, perf) con consentimiento y control plane (ADR-027);
el dashboard (ADR-047) la consume en vivo.
**Consecuencia**: El juego se observa en producción como se observa en CI; el
tuning (ADR-092) y la IA aprenden de datos reales, no de opiniones.

### ADR-095: Modding surface = los tres caminos, sin lenguaje aparte
**Contexto**: Un lenguaje de scripting nuevo (Lua-like) agregaría un cuarto
camino que divide el runtime y la seguridad.
**Decisión**: El modding es **content packs (ADR-067) + plugins (ADR-054) +
blueprints (F8)** — los tres caminos de la visión. Sin lenguaje de scripting
nuevo sin un ADR explícito que lo justifique.
**Consecuencia**: Mods y contenido IA usan la misma superficie validada
(content trust ADR-042); el runtime no gana un cuarto mundo que mantener.

### ADR-096: Seguridad online por diseño
**Contexto**: En un online, el client es un atacante en la red; confiar en su
estado = cheats y exploits.
**Decisión**: **Server authority** (ADR-062) como base + validación de cada
paquete, nunca confiar en estado del client, content trust (ADR-042) para
contenido descargado, rate limits. Anti-cheat es producto; el engine da las
herramientas.
**Consecuencia**: El server valida todo lo que entra; el client es un terminal
de presentación + predicción. La seguridad se diseña con el netcode, no después.

### ADR-097: Compilación a escala
**Contexto**: El tiempo de compilación es el impuesto más silencioso del
proyecto: lo pagan todos, todos los días, incluida la IA en su loop (ADR-045).
**Decisión**: La compilación es **una métrica** como el frame: ccache (ya),
unity builds y PCH cuando duelan, distributed builds si la curva lo pide; el
harness mide y publica tiempos.
**Consecuencia**: El loop humano-IA se mantiene en minutos, no horas; la
velocidad de compilación tiene dueño y dashboard (ADR-047), como el rendimiento
del frame.

---

## 🔒 Convenios de Código (resumen)

| Concepto | Convención |
|---|---|
| Tipos | PascalCase (`Vec3`, `EntityHandle`, `GameLoop`) |
| Funciones | camelCase (`translate`, `rotateX`, `init`) |
| Miembros privados | `m_` prefijo (`m_position`) |
| Archivos | snake_case (`vec3.h`, `game_loop.cpp`) |
| Constantes | SCREAMING_SNAKE_CASE (`MAX_ENTITIES`, `FIXED_DT`) |
| Headers | `#pragma once`; sin include guards redundantes |
| Documentación | Doxygen breve en headers públicos |

Detalles completos en `docs/rules/`.

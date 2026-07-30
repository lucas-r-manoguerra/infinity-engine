# Infinity Engine — MVP y Roadmap

> Producto Mínimo Viable + Camino hacia la Visión Final

---

## 🎯 MVP (Milestone 1 — Actual)

El MVP es **un engine que compila, corre, renderiza algo en pantalla y tiene ECS funcional**.
No es un juguete — es la base sólida sobre la que TODO lo demás se construye.

### Requisitos del MVP

| Componente | Estado | Descripción |
|---|---|---|
| Core: Engine lifecycle | ✅ | Engine::init → run → shutdown |
| Core: Game Loop | ✅ | Fixed timestep, variable rendering |
| Core: Time | ✅ | Clock, delta time, FPS counter |
| Core: Memory | ✅ | Arena allocator, pool allocator |
| Math: Vec3 | ✅ | Operaciones básicas + normalización |
| Math: Mat4 | ✅ | Transformaciones, proyecciones |
| Math: Transform | ✅ | TRS (translate/rotate/scale) |
| Platform: Window | ✅ | X11 directo, resize, close |
| Platform: Input | ✅ | Teclado (esc → salir) |
| Renderer: Software | ✅ | Framebuffer, clear, triangle fill |
| ECS: World | ✅ | Crear/destruir entidades |
| ECS: Component | ✅ | Registro type-safe |
| ECS: System | ✅ | Sistema iterativo sobre componentes |
| AI: Skeleton | 🔲 | Contexto de IA + estructura de agente |
| Blueprint: Skeleton | 🔲 | VM + Nodo + Graph (estructura vacía) |
| Tests: Engine | ✅ | Test de init/shutdown |
| Tests: Math | ✅ | Vec3, Mat4 operaciones |
| Tests: ECS | ✅ | World, entity lifecycle |

### Criterios de Éxito del MVP

```
zig build run    → abre ventana con triángulo renderizado, ESC cierra
zig build test   → todos los tests pasan, 0 leaks
src/             → ~25 archivos, cada uno < 200 líneas
```

---

## 🗺️ Roadmap Completo

### Milestone 2 — Renderer 3D Real
- Vulkan backend (hardware acceleration)
- Pipeline cache, shader compilation
- Mesh rendering (OBJ loader)
- Camera system (perspective, orbit)
- Depth buffer, backface culling

### Milestone 3 — Asset Pipeline
- Async asset loading
- Texture support (PNG, KTX2)
- Material system
- Asset registry hot-reload

### Milestone 4 — 3D Scene & Physics
- Scene graph (transform hierarchy)
- Frustum culling
- Physics: broadphase + narrowphase
- Collision detection (AABB, sphere)

### Milestone 5 — Audio Engine
- Audio buffer streaming
- 3D spatial audio
- Audio mixer with channels

### Milestone 6 — AI Core
- Agent context provider (ECS + scene state)
- LLM integration layer (model-agnostic)
- Prompt templates for game generation
- Code generation from natural language

### Milestone 7 — Blueprint VM
- Node graph compiler
- Blueprint → Zig transpiler
- Runtime execution context
- Visual debugger

### Milestone 8 — Editor Suite
- Blueprint editor (visual node graph)
- Level editor
- Debug tools (profiler, memory inspector)
- Project manager

### Milestone 9 — Optimization Pass
- Cache-aware ECS iteration
- Job system (parallelism)
- GPU-driven rendering
- Memory streaming for open worlds

### Milestone 10 — Release & Ecosystem
- Cross-platform (Windows, macOS, Linux)
- Package/asset store
- Documentation & tutorials
- Community SDK

---

## 📐 Principios de Desarrollo

### One File = One Task (HARD RULE)
Cada archivo es una unidad atómica de responsabilidad. Si necesitás más de 200 líneas,
partí el archivo. Punto. Esto no es negociable porque:
- La IA entiende mejor archivos pequeños y enfocados
- El diff en PRs es legible y revisable
- La reutilización es natural y obvia

### Testing-Driven
- `src/` y `tests/` son espejos: `src/math/vec3.zig` → `tests/math/vec3_test.zig`
- Cada `pub fn` pública tiene al menos un test
- Los tests se ejecutan en cada `zig build test`

### Sin Dependencias Innecesarias
El MVP usa **cero dependencias externas**. X11 es parte del sistema operativo.
Cada dependencia que agreguemos tiene que justificar su peso:
- ¿Resuelve un problema real?
- ¿Podemos escribir esa funcionalidad nosotros con menos overhead?
- ¿Es mantenible a largo plazo?

### La IA No Es un Afterthought
La integración de IA no se "agrega después". Desde el MVP el `ai/` module existe
con su estructura y tipos. Cuando llegue el momento, la IA ya tiene un lugar donde
vivir.

---

## 📊 Métricas de Progreso

```
MVP:         ~2,500 líneas de Zig,   ~25 archivos,   0 deps externas
M2:          ~8,000 líneas de Zig,   ~50 archivos,   Vulkan SDK
M3:          ~15,000 líneas de Zig,  ~80 archivos,   stb_image (sola dep)
M4:          ~25,000 líneas de Zig,  ~120 archivos
M5:          ~35,000 líneas de Zig,  ~160 archivos
M6+IA:       ~50,000 líneas de Zig,  ~220 archivos
M7+Blueprint:~70,000 líneas de Zig,  ~300 archivos
M10+Release: ~120,000 líneas de Zig, ~500+ archivos
```

---

> **El MVP no es el destino. Es la plataforma de lanzamiento.**
> Cada milestone está diseñado para ser usado inmediatamente después de completado.

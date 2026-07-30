# Infinity Engine — Arquitectura

> Documentación arquitectónica del Infinity Engine.
> Este archivo crece con el proyecto. Refleja las decisiones actuales.

---

## 📁 Estructura de Directorios

```
Infinity-Engine/
├── src/                    # Código fuente del engine
│   ├── main.zig            # Entry point (executable)
│   ├── root.zig            # Public API (library)
│   ├── core/               # Engine core
│   │   ├── engine.zig      # Lifecycle: init → run → shutdown
│   │   ├── loop.zig        # Fixed timestep game loop
│   │   ├── time.zig        # Clock, delta, FPS
│   │   ├── memory.zig      # Arena + Pool allocators
│   │   └── error.zig       # Engine error set
│   ├── math/               # Matemáticas 3D
│   │   ├── vec3.zig        # Vector 3D
│   │   ├── vec4.zig        # Vector 4D
│   │   ├── mat4.zig        # Matriz 4x4
│   │   ├── quat.zig        # Quaternion
│   │   └── transform.zig   # TRS transform
│   ├── platform/           # Abstracción de plataforma
│   │   ├── window.zig      # Window interface
│   │   ├── input.zig       # Input state
│   │   ├── x11.zig         # Linux X11 backend
│   │   └── context.zig     # Platform context (init/cleanup)
│   ├── renderer/           # Sistema de renderizado
│   │   ├── renderer.zig    # Renderer abstraction
│   │   ├── software.zig    # Software framebuffer
│   │   ├── vulkan.zig      # Vulkan backend (future)
│   │   └── triangle.zig    # Debug triangle drawing
│   ├── ecs/                # Entity Component System
│   │   ├── world.zig       # World container
│   │   ├── entity.zig      # Entity handle
│   │   ├── component.zig   # Component registry
│   │   ├── system.zig      # System interface
│   │   └── query.zig       # Entity query iterator
│   ├── ai/                 # Native AI integration
│   │   ├── core.zig        # AI context & config
│   │   ├── agent.zig       # Agent abstraction
│   │   ├── prompt.zig      # Prompt templates
│   │   └── codegen.zig     # Code generation bridge
│   ├── blueprint/          # Blueprint visual scripting
│   │   ├── vm.zig          # Blueprint virtual machine
│   │   ├── node.zig        # Node definition
│   │   ├── graph.zig       # Node graph
│   │   └── compiler.zig    # Blueprint → Zig compiler
│   └── asset/              # Asset pipeline
│       ├── loader.zig      # Async asset loading
│       ├── mesh.zig        # Mesh data
│       └── texture.zig     # Texture data
├── tests/                  # Tests mirror src/ structure
│   ├── core/
│   ├── math/
│   ├── ecs/
│   └── ...
├── docs/                   # Documentación
│   ├── VISION.md           # Visión del producto
│   ├── MVP.md              # MVP y roadmap
│   └── ARCHITECTURE.md     # Este archivo
├── build.zig               # Build system
├── build.zig.zon           # Package manifest
└── opencode.json           # OpenCode config
```

### Regla: "Un archivo = una tarea"

Cada archivo en `src/` representa **exactamente UNA abstracción o responsabilidad**.

✅ **Bien**: `src/math/vec3.zig` — solo define Vec3 y sus operaciones
✅ **Bien**: `src/ecs/entity.zig` — solo define Entity
❌ **Mal**: `src/math.zig` con Vec3, Mat4, Quat y Transform todo junto
❌ **Mal**: `src/ecs.zig` con World + Entity + Component + System

Si un archivo pasa de ~200 líneas, es hora de partir.

---

## 🧩 Diagrama de Dependencias (Actual)

```
main.zig
  └── root.zig
        ├── core/engine.zig
        │     ├── core/loop.zig
        │     │     └── core/time.zig
        │     ├── core/memory.zig
        │     ├── platform/window.zig
        │     │     └── platform/x11.zig
        │     ├── platform/input.zig
        │     └── renderer/renderer.zig
        │           └── renderer/software.zig
        ├── ecs/world.zig
        │     ├── ecs/entity.zig
        │     ├── ecs/component.zig
        │     ├── ecs/system.zig
        │     └── ecs/query.zig
        ├── math/ (independiente)
        ├── ai/  (esqueleto)
        └── blueprint/ (esqueleto)
```

### Reglas de Dependencia

1. **Los módulos de bajo nivel no conocen los de alto nivel**
   - `math/` no sabe que existe `ecs/` o `renderer/`
   - `core/` no sabe que existe `ai/` o `blueprint/`

2. **Plataforma siempre abstraída**
   - `platform/window.zig` define la interfaz
   - `platform/x11.zig` implementa para Linux
   - El runtime nunca importa x11.zig directamente

3. **AI habla con todo a través de contexto serializado**
   - AI no importa ECS ni Renderer directamente
   - AI recibe un `ContextSnapshot` serializado

---

## 🧠 Decisiones Arquitectónicas

### ADR-001: Software Renderer para MVP
**Contexto**: No hay Vulkan SDK instalado. Necesitamos algo que renderice en pantalla desde el día 1.
**Decisión**: Framebuffer por software vía X11 Shm (shared memory).
**Consecuencia**: Podemos iterar rápido sin depender de drivers GPU. Cuando Vulkan esté listo, el renderer software queda como fallback.

### ADR-002: Allocators Explícitos
**Contexto**: Zig permite allocators personalizados. Muchos engines en otros lenguajes usan malloc global.
**Decisión**: Cada subsistema recibe su allocator. El engine principal usa un arena para frame-scoped allocations.
**Consecuencia**: Memoria predecible. Zero GC pauses. Fácil de trackear leaks.

### ADR-003: Fixed Timestep Game Loop
**Contexto**: El game loop determina cómo actualizamos lógica y renderizamos.
**Decisión**: Fixed timestep a 60Hz para updates, renderización desacoplada a máxima frecuencia.
**Consecuencia**: Física determinista. Misma simulación en diferentes framerates.

### ADR-004: ECS Archetype-based (futuro)
**Contexto**: Para el MVP usamos un ECS simple de sparse sets. Para AAA necesitamos archetypes.
**Decisión**: MVP con diseño simple pero con la misma interfaz. En M2 migramos a archetypes sin cambiar la API pública.
**Consecuencia**: Podemos construir features arriba sin reescribir después.

---

## 🔒 Convenios de Código

### Naming
- **Tipos**: PascalCase (`Vec3`, `EntityHandle`, `GameLoop`)
- **Funciones**: camelCase (`translate`, `rotateX`, `init`)
- **Archivos**: snake_case (`vec3.zig`, `game_loop.zig`)
- **Constantes**: SCREAMING_SNAKE_CASE (`MAX_ENTITIES`, `FIXED_DT`)

### Estructura de Archivo
```zig
//! Doc coment: qué hace este archivo

const std = @import("std");
const OtherMod = @import("other_mod");

// Constantes de módulo
const MAX_COUNT = 1024;

// Tipos públicos
pub const MyType = struct {
    field: u32,

    pub fn init() MyType { ... }
};

// Tests
test "my type should do x" { ... }
```

### Tests
- Espejan la estructura de `src/`
- Nombre del test describe el comportamiento esperado
- Usar `std.testing` (zero dependencies for tests)

# Infinity Engine — Visión del Producto

> Un motor de videojuegos AAA escrito en C++ desde cero, con integración nativa de IA,
> un sistema de Blueprints para scripting visual, cobertura online de entrada (MMO/RPG)
> y un renderer diseñado para escalar a clase Nanite/Lumen.

## 🌟 La Visión Final

Infinity Engine es un motor de videojuegos AAA donde **cada capa está diseñada para
maximizar control, rendimiento y flexibilidad**. No es un engine "más": es un engine
donde cada decisión arquitectónica responde a tres principios y a tres ambiciones:
juegos online de entrada, mundo procedural y un renderer que escala a clase AAA
moderna (geometría virtualizada e iluminación global dinámica).

### 1. Control Total

C++ nos da control absoluto sobre memoria, rendimiento y ejecución. Sin runtime oculto,
sin GC imposible de trackear, sin abstracciones que esconden el costo real.
Cada ciclo de CPU, cada byte de memoria, cada llamada al sistema operativo es
**intencional y medible**.

### 2. IA Nativa

La IA no es un plugin ni una API externa. Es parte del engine. El sistema de agentes
de IA entiende tu proyecto, tu código y tus Blueprints, y puede:
- **Generar gameplay desde lenguaje natural**: "creá un boss que ataque en fases"
- **Escribir código C++ optimizado** para el engine
- **Compilar Blueprints a código nativo** sin intermediarios
- **Asistir en debugging** con contexto completo del runtime

### 3. Tres Caminos, Un Motor

El mismo juego se puede construir de tres formas, o combinándolas:

| Camino | Quién | Para qué |
|--------|-------|----------|
| **IA-only** | Creadores, diseñadores, prototipado rápido | Describís lo que querés y la IA lo genera |
| **C++-code** | Ingenieros, optimización | Control total sobre cada aspecto |
| **Blueprints** | Diseñadores, gameplay programadores | Lógica visual sin perder rendimiento |

Los tres caminos convergen en el mismo runtime optimizado. No hay "capa extra" por
usar Blueprints — se compilan a código C++ nativo.

### 4. Online, Procedural y Renderer-Class desde el Día 1

No son features futuras: son **restricciones de forma** que condicionan las decisiones
de hoy (ADR-062, ADR-072 a 097).

- **Online de entrada (MMO/RPG/MMORPG)**: el server es la verdad del mundo; los clients
  predicen y rebobinan. El mundo es **particionable** (zonas/shards) y **persistente**.
  Los inputs y comandos son la unidad de red desde el día 1.
- **Mundo procedural**: la generación es data determinista — mismo seed, mismo mundo.
  El mundo abierto se streamea por chunks con presupuesto; la IA y el procgen comparten
  el mismo pipeline de generación.
- **Renderer clase Nanite/Lumen**: targets GPU-driven, geometría virtualizada e
  iluminación global dinámica. No se agregan al final: condicionan cómo se representa
  la escena, cómo se modelan los meshes y cómo se elige el modelo de luz desde F4.

## 🏗️ Pilares Arquitectónicos

### Modularidad desde el Día 1

Cada subsistema es un módulo independiente con interfaces claras. El renderer no
conoce el ECS. El ECS no conoce la entrada del usuario. La IA habla con todo a
través de un bus de contexto. En CMake, cada módulo es un **static library target**:
las reglas de dependencia se cumplen en tiempo de link, no por buena voluntad.

```
┌──────────────────────────────────────────────────┐
│                   Infinity Engine                 │
├──────────────────────────────────────────────────┤
│  ┌──────────┐  ┌──────────┐  ┌────────────────┐ │
│  │  Core     │  │  Runtime  │  │  Editor Suite  │ │
│  │  - ECS    │  │  - Loop  │  │  - Blueprint   │ │
│  │  - Math   │  │  - Time  │  │  - Level       │ │
│  │  - Memory │  │  - Job   │  │  - Debug       │ │
│  └────┬─────┘  └────┬─────┘  └───────┬────────┘ │
│       │              │                │          │
│  ┌────▼──────────────▼────────────────▼────────┐ │
│  │           Platform Abstraction Layer         │ │
│  │  ┌──────┐  ┌───────┐  ┌──────┐  ┌────────┐ │ │
│  │  │X11/  │  │Vulkan │  │Audio │  │Input   │ │ │
│  │  │Wayland│  │       │  │      │  │        │ │ │
│  │  └──────┘  └───────┘  └──────┘  └────────┘ │ │
│  └──────────────────────────────────────────────┘ │
│                                                    │
│  ┌──────────────────────────────────────────────┐  │
│  │          AI Integration Layer                 │  │
│  │  ┌─────────┐  ┌────────┐  ┌───────────────┐  │  │
│  │  │Agent    │  │Prompt  │  │Code Generator │  │  │
│  │  │System   │  │Engine  │  │(Blueprint/C++)│  │  │
│  │  └─────────┘  └────────┘  └───────────────┘  │  │
│  └──────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
```

### One File = One Task

No archivos de 2000 líneas. Cada archivo representa **exactamente una responsabilidad**.
Si un archivo necesita más de ~300 líneas, se divide. Esto no es burocrático — es
necesario para que la IA pueda entender, modificar y generar código con precisión.

### Testing First

No se escribe una función sin su test. Punto. No hay "lo pruebo después".
El testing es parte del flujo de desarrollo, no una etapa.

## 🎯 Promesas del Producto Final

- **Rendimiento AAA**: 60+ FPS en hardware moderno, optimización cache-aware
- **Latencia cero en las herramientas**: editor responsive, compilación incremental
- **IA contextual**: el asistente entiende TODO tu proyecto, no solo el archivo abierto
- **Blueprint first-class**: no son azúcar visual, son código real compilado a nativo
- **Multi-plataforma**: Linux, Windows, macOS desde el vamos (MVP en Linux)
- **Online de entrada**: multiplayer con server authority, mundo persistente y particionable (MMO/RPG)
- **Mundo procedural**: generación determinista por seed, streaming por chunks con presupuesto
- **Renderer-class**: geometría virtualizada (Nanite-like) e iluminación global dinámica (Lumen-like)

---

> "No estamos construyendo un engine. Estamos construyendo **cómo se van a construir
> los juegos del futuro**."

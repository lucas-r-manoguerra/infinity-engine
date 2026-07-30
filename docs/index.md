---
okf_version: "0.2"
---

# Infinity Engine

Un motor de juegos modular, renderizado por software, construido desde cero en **Zig 0.16.0** sobre **x86_64 Linux**. Sin GPU, sin motor existente — solo X11, un puente C, y un framebuffer de píxeles.

## Por qué

Hoy la mayoría del desarrollo de juegos asume DirectX, Vulkan o WebGPU. Este proyecto existe para demostrar lo que se puede lograr con renderizado puro en CPU: control total, cero dependencias de drivers, y un entendimiento arquitectónico profundo que se traduce directamente a programación en GPU. Cada concepto acá — framebuffers, rasterización baricéntrica, Z-buffering, sampleo de texturas — es el mismo concepto que las GPUs implementan en hardware.

## Qué esperar

| Layer | Status | Description |
|-------|--------|-------------|
| X11 Window | ✅ | Ventana 800×600, ESC/cerrar para salir |
| Framebuffer | ✅ | Buffer de píxeles BGRA32, blit con XPutImage |
| Software Renderer | ✅ | Rasterizador baricéntrico por scanline |
| Gouraud Shading | ✅ | Interpolación de color por vértice |
| Z-buffer | 🔲 | Ordenamiento por profundidad para superposición correcta |
| Textures | 🔲 | Interpolación UV + sampleo de imagen |
| ECS / Scene Graph | 🔲 | Arquitectura Entity-Component-System |
| Audio | 🔲 | Mezclador por software |

## Mapa del proyecto

```
├── src/
│   ├── main.zig          ← Entry point, event loop, render scene
│   └── x11_bridge.c      ← C bridge: all X11 calls via dlopen/dlsym
├── build.zig             ← Build system (static .o + -ldl link)
├── docs/
│   ├── index.md          ← Estás acá
│   ├── log.md            ← Change log
│   ├── architecture/     ← Module architecture docs
│   ├── decisions/        ← Architecture Decision Records
│   ├── performance/      ← Performance baselines & optimization
│   └── testing/          ← Test strategy & coverage
└── zig-out/bin/          ← Build output
```

## Principios

1. **Zero magic** — cada byte en el framebuffer está escrito explícitamente por nuestro código
2. **Dependencias mínimas** — una dependencia en runtime: `libX11.so.6` (cargada en runtime, no linkeada)
3. **Modular desde el día uno** — el puente C aísla el código específico del SO; Zig maneja la lógica del motor
4. **Decisiones documentadas** — cada decisión arquitectónica tiene su fundamento registrado

## Inicio rápido

```bash
zig build && zig build run
```

Presioná **ESC** o cerrá la ventana para salir.

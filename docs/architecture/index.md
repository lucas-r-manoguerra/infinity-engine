# Arquitectura

| Módulo | Estado | Descripción |
|--------|--------|-------------|
| [C Bridge](x11-bridge.md) | ✅ | Todas las llamadas X11 vía dlopen/dlsym, invocadas desde Zig |
| [Framebuffer](framebuffer.md) | ✅ | Buffer de píxeles BGRA32, XPutImage blit a ventana |
| [Software Renderer](renderer.md) | ✅ | Rasterizador por scanline con coordenadas baricéntricas y sombreado Gouraud |
| [Build System](build-system.md) | ✅ | Compilación estática `.o`, enlace `-ldl` |
| Z-buffer | 🔲 | Buffer de profundidad para ordenamiento correcto de triángulos |
| Texture Unit | 🔲 | Interpolación UV + muestreo bilineal |
| ECS / Scene Graph | 🔲 | Entity-Component-System para objetos de juego |

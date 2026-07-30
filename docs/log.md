## 2026-07-29

- **ADR-005** Z-buffer para ordenamiento por profundidad: depth buffer de 1.83 MB con interpolación de Z y early Z-test
- **ADR-004** Sombreado Gouraud: interpolación de color por vértice mediante pesos baricéntricos
- **ADR-003** Formato de píxeles BGRA32 elegido para alineación con ZPixmap de XPutImage
- **Feature** Título de ventana configurado con XStoreName
- **Feature** Escena demo: triángulo Gouraud + diamante rotatorio (5 triángulos en total)
- **Feature** Z-buffer: 3 capas de profundidad (fondo gris z=0.8, triángulo z=0.5, diamante z=0.1)
- **Feature** Pipeline 3D completo: modelo → vista → proyección → clip → back-face cull → viewport. Cubo Gouraud texturado por cara (12 triángulos, 6 colores)
- **Math** Mat4.inverse agregado a la librería matemática

## 2026-07-28

- **ADR-002** Renderizador por software en vez de GPU — renderizado solo en CPU para control total
- **ADR-001** Puente C en vez de Zig DynLib — workaround para un bug de convención de llamada en Zig 0.16.0
- **Feature** Ventana X11 con bucle de eventos (ESC, WM_DELETE_WINDOW, DestroyNotify)
- **Feature** Asignación de framebuffer y pipeline de blit con XPutImage
- **Feature** Triángulo relleno con rasterizador baricéntrico por scanline
- **Build** El puente C compila como `.o` estático via `build.zig`, linkeado con `-ldl`

# Regla 03 — Memoria y Ownership

> La memoria es un contrato explícito, no un accidente. Decisiones y convenciones.

## Decisión: Allocators explícitos (ADR-005)

- Cada subsistema recibe un `Allocator` en su constructor/init. Nada de `new` global suelto.
- El engine principal usa un arena para allocaciones frame-scoped (se resetea cada frame).
- `malloc`/`new` solo dentro de la implementación de un allocator, nunca en lógica de negocio.

## Decisiones de ownership

| Escenario | Decisión |
|---|---|
| Objeto con dueño claro | `std::unique_ptr` |
| Objeto compartido (raro en engine) | `std::shared_ptr` — justificar su uso |
| Referencia no propietaria | raw pointer o referencia; documentar lifetime |
| Buffers GPU/asset | RAII wrapper con release explícito |
| Callbacks/observers | raw pointers registrados/desregistrados; nunca shared_ptr en callbacks |

## Reglas duras

1. **RAII**: recursos (memoria, window, X11 context, Vulkan) se liberan en destructores.
2. **No leaks**: ASan en debug lo verifica. Un test que leakea = test que falla.
3. **Hot paths (cada frame)**: cero allocaciones después del init. Reservar buffers upfront.
4. **Containers STL**: usar con allocator propio del módulo (`Allocator`); evitar `bad_alloc`
   como mecanismo de error (no hay excepciones — ver regla 04).
5. **Ownership visible**: un método que devuelve un raw pointer debe documentar quién es dueño.
6. **Nada de singletons con estado mutable** — el estado vive en objetos explícitos.
7. **Memory budgets** (ADR-034): presupuesto declarado por subsistema; superarlo es
   una alerta visible, no una sorpresa.

## Verificación

- Debug: ASan + UBSan activos (`cmake --preset debug`).
- Release: benchmarks con la misma disciplina (leaks también son bugs en release).

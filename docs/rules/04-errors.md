# Regla 04 — Manejo de Errores

> Decisión (ADR-003): sin excepciones. Errores explícitos con `std::expected`.

## El modelo

| Tipo de error | Mecanismo |
|---|---|
| Recuperable (init falla, archivo no existe, recurso agotado) | `std::expected<T, E>` |
| Error de programación (invariante rota, uso incorrecto de API) | `assert` en debug / panic en release |
| Alocación falla en hot path | Nunca puede pasar: buffers reservados upfront (regla 03) |

## Reglas duras

1. **`-fno-exceptions`** en todo el proyecto. No hay `try/catch`, no hay `throw`.
2. Una función que puede fallar **devuelve `std::expected`** — nunca esconde el error
   en un valor mágico (`-1`, `nullptr` sin contexto) ni en un flag global.
3. El tipo de error `E` es un enum por subsistema con categoría clara
   (init, io, resource, invalid_state, not_supported).
4. El caller **debe** manejar el error en el nivel apropiado:
   - Sube el error si este nivel no puede decidir.
   - Traduce el error si va a cruzar un límite de módulo (no filtra errores internos).
   - Cierra el error en el límite del sistema (main, callbacks) con logging y salida limpia.
5. **No tragar errores**: `(void)result` sin justificación documentada = review failure.
6. `assert` solo para invariantes de programación; nunca para validar input de usuario.

## Convención

- El resultado exitoso se usa directo; el error se inspecciona explícitamente en el `else`.
- Errores de API pública: documentados en el header con su tipo `E` y cuándo ocurre cada error.
- Logging de errores en el límite, no en cada capa intermedia (evita ruido duplicado).

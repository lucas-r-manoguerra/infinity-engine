# Regla 06 — Testing

> Decision (ADR-008): doctest vendored + CTest. **Testing First** es ley.

## El flujo

1. Se escribe el test que describe el comportamiento deseado (rojo).
2. Se implementa la feature mínima para que pase (verde).
3. Se refactoriza manteniendo verde.
4. `ctest` completo verde + 0 leaks (ASan) = la tarea está lista.

## Convenciones

- **Estructura espejo**: `engine/<mod>/` → `tests/<mod>/`.
  `engine/math/include/.../vec3.h` → `tests/math/vec3_test.cpp`.
- **Un ejecutable por módulo**: `infinity_<mod>_tests`, registrado en CTest.
- **Nombre del test describe el comportamiento**, no la función:
  `"Vec3 normalized keeps direction and returns unit length"`.
- **Cada API pública tiene al menos un test.** APIs privadas: test indirecto vía la pública.
- **Caso feliz + casos borde + caso de error** para cada función que lo amerite.
- **Checksums deterministas**: el renderer se testea contra checksums de framebuffer,
  nunca contra "se ve bien".
- **Zero leaks**: ASan en el preset debug lo verifica. Un test que leakea = rojo.

## Lo que se testea

| Capa | Qué | Cómo |
|---|---|---|
| math | operaciones, edge cases (división por cero, NaN, ángulos límite) | unit tests directos + property tests (ADR-017) |
| core | allocators (stress), loop (timestep), thread pool (concurrencia) | unit + stress + fault injection (ADR-016) |
| platform | ciclo de ventana, input | tests de humo |
| ecs | lifecycle de entidades, queries (vacías, llenas, filtradas) | unit tests + read/write sets (ADR-018) |
| renderer | checksums de framebuffer, tiles | determinista |
| ai | round-trip de serialización | unit tests + property tests (ADR-017) |
| blueprint | graph → C++ → compila → ejecuta | integration test + schema versioning (ADR-022) |
| assets (F9) | streaming por chunks, procgen: mismo seed → mismo mundo | determinista + property tests (ADR-076/077) |
| network (F15) | red simulada: latencia/pérdida/jitter, server authority | integration + escenarios de red (ADR-074) |

## Fault injection (ADR-016)

- Los fallos (alloc, IO, init) se inyectan de forma controlada en tests.
- **Cada rama de error declarada con `std::expected` tiene al menos un test que la ejercita.**
- Un error declarado pero no testeado = defecto conocido.

## Property-based tests + fuzzing (ADR-017)

- Invariantes con entradas aleatorias: `inverse(M)*M == I`, quats unitarios,
  round-trip serialización == identidad, matriz degenerada no crashea.
- Se escriben como propiedades, no como ejemplos aislados.

## Reglas duras

1. No hay "lo pruebo después". Una función sin test no existe.
2. Los tests corren en CI en cada push (build + ctest + sanitizers).
3. Un test flaky se arregla o se elimina — nunca se ignora.
4. Los benchmarks NO son tests de corrección: miden, no afirman (regla 08).
5. Las rutas de error no se testean "a mano": se usa fault injection (ADR-016).
6. Las invariantes de math y serialización se propierten (ADR-017), no se ejemplifican solas.

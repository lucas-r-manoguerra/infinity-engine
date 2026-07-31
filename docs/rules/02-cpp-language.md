# Regla 02 — C++23 y Estilo de Código

> El código se escribe en C++23 moderno, consistente y legible para humanos y agentes.

## Estándar y toolchain

- **C++23** (`CMAKE_CXX_STANDARD 23`). No se usan modules (`import`) — compilación clásica.
- Compiladores: GCC 14+ / Clang 20+. Builds con `-Wall -Wextra -Werror`.
- `-fno-exceptions`. Sin RTTI salvo decisión explícita por módulo.

## Naming

| Concepto | Convención | Ejemplo |
|---|---|---|
| Tipos y clases | PascalCase | `Vec3`, `EntityHandle`, `GameLoop` |
| Funciones/métodos | camelCase | `translate()`, `rotateX()`, `init()` |
| Miembros privados | `m_` prefijo | `m_position` |
| Parámetros/locales | camelCase | `deltaTime`, `entityCount` |
| Archivos | snake_case | `vec3.h`, `game_loop.cpp` |
| Constantes | SCREAMING_SNAKE_CASE | `MAX_ENTITIES`, `FIXED_DT` |
| Namespaces | snake_case | `namespace infinity::math` |

## Estructura de archivo

```cpp
// infinity/math/vec3.h
#pragma once

namespace infinity::math {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    [[nodiscard]] static Vec3 zero();
    [[nodiscard]] Vec3 normalized() const;
    // ...
};

} // namespace infinity::math
```

```cpp
// src/vec3.cpp
#include "infinity/math/vec3.h"

#include <cmath>

namespace infinity::math {

Vec3 Vec3::normalized() const {
    const float len = std::sqrt(x * x + y * y + z * z);
    ...
}

} // namespace infinity::math
```

## Reglas duras

1. **Headers públicos**: `#pragma once`, self-contained, documentados (Doxygen breve).
2. **`const` por defecto**: métodos `const` si no mutan; parámetros por `const&` para tipos grandes.
3. **`[[nodiscard]]`** en funciones cuyo resultado no se puede ignorar (error handling, cálculos puros).
4. **`nullptr`, no `NULL`/`0`.** `using`, no `typedef`.
5. **Nada de `auto` para tipos triviales** (`auto x = 5` prohibido); `auto` solo para tipos complejos/iterators.
6. **Inicialización brace** `{}` por defecto — zero-init gratis.
7. **Prohibido**: macros de función, variables globales mutables, `friend` salvo casos justificados, herencia profunda (>3 niveles).
8. **No usar `std::cout`/`printf` para logging** — usar el sistema de logging del engine
   (structured logging, ADR-046: niveles, canales, sinks).

## Clang-Format / Clang-Tidy

- `.clang-format` y `.clang-tidy` en la raíz son ley: `scripts/format.sh` aplica ambos.
- En CI se verifica `clang-format --dry-run --Werror`; un diff de formato = build rojo.
- `clang-tidy` corre con los checks del proyecto; warnings nuevos = build rojo.

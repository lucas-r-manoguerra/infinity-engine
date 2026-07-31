add_library(infinity_math STATIC
    src/vec2.cpp
    src/vec3.cpp
    src/vec4.cpp
    src/mat4.cpp
    src/quat.cpp
    src/transform.cpp
)

target_include_directories(infinity_math PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(infinity::math ALIAS infinity_math)

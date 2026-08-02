add_library(infinity_renderer STATIC
    src/camera.cpp
    src/render_target.cpp
    src/renderer.cpp
    src/software/software_backend.cpp
)

target_include_directories(infinity_renderer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
# Private include root so the implementation can reach src/<backend>/ headers.
target_include_directories(infinity_renderer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# Backend selection (rule 01): the software backend (ADR-004/030) is the only
# one wired today. Vulkan ships in F4.5 and flips the compile-time marker
# consulted in src/renderer.cpp.
target_compile_definitions(infinity_renderer PRIVATE INFINITY_RENDERER_SOFTWARE=1)

# The public API returns core types (Allocator, Expected, ErrorCategory), so
# core is part of the interface: PUBLIC keeps include dirs and symbols on
# every consumer link.
#
# F4.10: cameras are first-class (ADR-051) and their public header returns
# infinity_math types (Mat4, Quat, Vec3) and builds matrices from them, so
# math is part of the renderer interface too.
target_link_libraries(infinity_renderer PUBLIC infinity_core infinity_math)

add_library(infinity::renderer ALIAS infinity_renderer)

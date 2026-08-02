# Backend selection (rule 01): X11 is the native window backend on Linux (peer
# of Win32 on Windows, not a vendored dependency - ADR-061) and is used
# whenever its development files are present. Otherwise the engine falls back
# to the headless backend (ADR-030) so the renderer, ECS and CI stay hermetic
# on machines without X11. Exactly one marker is defined below - the branch
# consulted in src/window.cpp.
find_package(X11)

if(X11_FOUND)
    set(INFINITY_PLATFORM_BACKEND_SOURCES src/x11/x11_window.cpp)
    set(INFINITY_PLATFORM_BACKEND_DEFINE INFINITY_PLATFORM_X11=1)
    message(STATUS "Infinity Engine platform backend: X11")
else()
    set(INFINITY_PLATFORM_BACKEND_SOURCES src/headless/headless_window.cpp)
    set(INFINITY_PLATFORM_BACKEND_DEFINE INFINITY_PLATFORM_HEADLESS=1)
    message(STATUS "Infinity Engine platform backend: headless (X11 not found)")
endif()

add_library(infinity_platform STATIC
    ${INFINITY_PLATFORM_BACKEND_SOURCES}
    src/input.cpp
    src/platform.cpp
    src/window.cpp
)

target_include_directories(infinity_platform PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
# Private include root so the implementation can reach src/<backend>/ headers.
target_include_directories(infinity_platform PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(infinity_platform PRIVATE ${INFINITY_PLATFORM_BACKEND_DEFINE})

# The public API returns core types (Allocator, Expected, ErrorCategory), so
# core is part of the interface: PUBLIC keeps include dirs and symbols on
# every consumer link.
target_link_libraries(infinity_platform PUBLIC infinity_core)

if(X11_FOUND)
    # Xlib is backend implementation detail: the public Window API never
    # exposes X11 types, so the link stays PRIVATE. CMake still propagates it
    # to consumers of this static library for linking only.
    target_link_libraries(infinity_platform PRIVATE X11::X11)
endif()

add_library(infinity::platform ALIAS infinity_platform)

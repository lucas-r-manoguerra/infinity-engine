add_library(infinity_platform STATIC
    src/headless/headless_window.cpp
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

# Backend selection (rule 01): the headless backend (ADR-030) is the only one
# wired today. X11 ships in F3.1 and flips the compile-time marker consulted
# in src/window.cpp.
target_compile_definitions(infinity_platform PRIVATE INFINITY_PLATFORM_HEADLESS=1)

# The public API returns core types (Allocator, Expected, ErrorCategory), so
# core is part of the interface: PUBLIC keeps include dirs and symbols on
# every consumer link.
target_link_libraries(infinity_platform PUBLIC infinity_core)

add_library(infinity::platform ALIAS infinity_platform)

add_library(infinity_core STATIC
    src/version.cpp
)

target_include_directories(infinity_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(infinity::core ALIAS infinity_core)

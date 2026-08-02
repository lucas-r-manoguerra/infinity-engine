cmake_minimum_required(VERSION 3.24)

project(InfinityEngine
    VERSION 0.1.0
    DESCRIPTION "Infinity Engine - C++23 game engine"
    LANGUAGES CXX)

# C++23 for every target. No modules: classic header/source compilation.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(InfinityToolchain)
include(InfinityWarnings)
include(InfinitySanitizers)

infinity_configure_toolchain()
infinity_configure_warnings()
infinity_configure_sanitizers()

# engine/ -> static libraries (infinity_<mod>), dependency direction
# enforced by target_link_libraries at link time (rule 01).
add_subdirectory(engine)

# apps/bench runs since F1 (infinity-bench); apps/sandbox arrives in F6.
add_subdirectory(apps)

# CTest (BUILD_TESTING option) + one test executable per module (rule 06).
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

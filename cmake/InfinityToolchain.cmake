# InfinityEngine - hermetic toolchain configuration (ADR-025).
#
# - Verifies the compiler baseline: GCC >= 14 or Clang >= 18 (C++23 support).
# - Applies the engine-wide language flags: -fno-exceptions and -fno-rtti
#   (rules 02 and 04). Test targets re-enable RTTI only if doctest ever needs
#   it (verified in F0: it does not).
# - Uses ccache as the compiler launcher when it is available.
# - Wires clang-tidy into the build when INFINITY_ENABLE_CLANG_TIDY is ON
#   (ci preset), with every finding promoted to an error.

option(INFINITY_ENABLE_CLANG_TIDY "Run clang-tidy during the build" OFF)

function(infinity_configure_toolchain)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
            message(FATAL_ERROR
                "Infinity Engine requires GCC >= 14 (found ${CMAKE_CXX_COMPILER_VERSION})")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
            message(FATAL_ERROR
                "Infinity Engine requires Clang >= 18 (found ${CMAKE_CXX_COMPILER_VERSION})")
        endif()
    else()
        message(FATAL_ERROR
            "Infinity Engine requires GCC >= 14 or Clang >= 18 (found ${CMAKE_CXX_COMPILER_ID})")
    endif()

    if(MSVC)
        add_compile_options(/EHs-c- /GR-)
    else()
        add_compile_options(-fno-exceptions -fno-rtti)
    endif()

    find_program(INFINITY_CCACHE_PROGRAM NAMES ccache)
    if(INFINITY_CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${INFINITY_CCACHE_PROGRAM}" PARENT_SCOPE)
        message(STATUS "ccache: ${INFINITY_CCACHE_PROGRAM}")
    endif()

    if(INFINITY_ENABLE_CLANG_TIDY)
        find_program(INFINITY_CLANG_TIDY_PROGRAM NAMES clang-tidy-18 clang-tidy)
        if(NOT INFINITY_CLANG_TIDY_PROGRAM)
            message(WARNING "INFINITY_ENABLE_CLANG_TIDY=ON but clang-tidy was not found; skipping")
            return()
        endif()
        set(CMAKE_CXX_CLANG_TIDY
            "${INFINITY_CLANG_TIDY_PROGRAM};--warnings-as-errors=*"
            PARENT_SCOPE)
        message(STATUS "clang-tidy: ${INFINITY_CLANG_TIDY_PROGRAM} (warnings as errors)")
    endif()
endfunction()

# Test harness (rule 06): one test executable per engine module, each
# registered with CTest. Mirror layout: engine/<mod>/ -> tests/<mod>/.
#
# infinity_add_test(<target> <source>...)
#   Creates a doctest executable with the vendored doctest included as a
#   SYSTEM directory (warnings from it never break -Werror builds) and
#   DOCTEST_CONFIG_NO_EXCEPTIONS so it compiles under -fno-exceptions
#   (ADR-008). One TU in each target defines
#   DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (see tests/core/main.cpp).
#
# doctest 2.4.11 is RTTI-free (verified in F0), so tests stay on the global
# -fno-rtti. If a future doctest requires RTTI, enable it only on test
# targets with: target_compile_options(<target> PRIVATE -frtti)

function(infinity_add_test target_name)
    add_executable(${target_name} ${ARGN})

    target_include_directories(${target_name} SYSTEM PRIVATE
        "${PROJECT_SOURCE_DIR}/third_party"
    )
    target_compile_definitions(${target_name} PRIVATE
        DOCTEST_CONFIG_NO_EXCEPTIONS
    )

    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_subdirectory(core)

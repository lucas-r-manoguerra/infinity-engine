# InfinityEngine - warning policy (rule 05).
#
# -Wall -Wextra everywhere, and -Werror by default in EVERY preset (debug,
# release and ci). Warnings from vendored headers are suppressed because
# tests include third_party/ as a SYSTEM directory.

option(INFINITY_ENABLE_WERROR "Treat compiler warnings as errors" ON)

function(infinity_configure_warnings)
    if(MSVC)
        add_compile_options(/W4)
        if(INFINITY_ENABLE_WERROR)
            add_compile_options(/WX)
        endif()
    else()
        add_compile_options(-Wall -Wextra)
        if(INFINITY_ENABLE_WERROR)
            add_compile_options(-Werror)
        endif()
    endif()
endfunction()

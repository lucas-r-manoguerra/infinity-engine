# InfinityEngine - sanitizer policy (rules 03 and 06).
#
# ASan + UBSan are wired into the debug and ci presets
# (INFINITY_ENABLE_SANITIZERS). UBSan is non-recovering: the first undefined
# behavior aborts the test instead of being silently hidden. Leaks are
# detected by ASan on Linux out of the box.

option(INFINITY_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

function(infinity_configure_sanitizers)
    if(NOT INFINITY_ENABLE_SANITIZERS)
        return()
    endif()
    if(MSVC)
        message(WARNING "Sanitizers are not wired for MSVC yet (targeted for F14)")
        return()
    endif()
    add_compile_options(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=undefined
    )
    add_link_options(-fsanitize=address,undefined)
endfunction()

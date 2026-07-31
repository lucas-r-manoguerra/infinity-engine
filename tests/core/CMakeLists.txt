infinity_add_test(infinity_core_tests
    main.cpp
    allocator_test.cpp
    arena_allocator_test.cpp
    diagnostics_test.cpp
    error_test.cpp
    fault_injector_test.cpp
    loop_test.cpp
    pool_allocator_test.cpp
    thread_pool_concurrency_test.cpp
    thread_pool_test.cpp
    time_test.cpp
    version_test.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(infinity_core_tests PRIVATE infinity_core Threads::Threads)

# clang-tidy-18 reports __cpp_concepts=201907L even under -std=c++23, so libstdc++ 15
# hides std::expected (gated on __cpp_concepts >= 202002L). clang 19+ fixed the value;
# these flags bridge the gap for the tidy gate only. Move to InfinityToolchain when
# the toolchain is clang >= 19.
target_compile_options(infinity_core_tests PRIVATE
    -U__cpp_concepts
    -D__cpp_concepts=202002L
    -Wno-builtin-macro-redefined
)

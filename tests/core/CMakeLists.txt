infinity_add_test(infinity_core_tests
    main.cpp
    allocator_test.cpp
    arena_allocator_test.cpp
    pool_allocator_test.cpp
    version_test.cpp
)

target_link_libraries(infinity_core_tests PRIVATE infinity_core)

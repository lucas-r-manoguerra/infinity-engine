infinity_add_test(infinity_core_tests
    main.cpp
    allocator_test.cpp
    arena_allocator_test.cpp
    budget_allocator_test.cpp
    diagnostics_test.cpp
    error_test.cpp
    fault_injector_test.cpp
    fs_test.cpp
    loop_test.cpp
    pool_allocator_test.cpp
    property_test_test.cpp
    system_registry_error_test.cpp
    system_registry_order_test.cpp
    thread_pool_concurrency_test.cpp
    thread_pool_test.cpp
    time_test.cpp
    utf8_test.cpp
    utf8_view_test.cpp
    utf8_property_test.cpp
    version_test.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(infinity_core_tests PRIVATE infinity_core Threads::Threads)

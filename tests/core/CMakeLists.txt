infinity_add_test(infinity_core_tests
    main.cpp
    allocator_test.cpp
    arena_allocator_test.cpp
    budget_allocator_test.cpp
    memory_budgets_test.cpp
    diagnostics_test.cpp
    error_test.cpp
    fault_injector_test.cpp
    fs_file_test.cpp
    fs_path_and_fault_test.cpp
    fs_tree_test.cpp
    loop_test.cpp
    pool_allocator_test.cpp
    profiler_test.cpp
    property_test_test.cpp
    system_registry_error_test.cpp
    system_registry_order_test.cpp
    thread_pool_barrier_test.cpp
    thread_pool_queue_test.cpp
    thread_pool_test.cpp
    time_test.cpp
    time_budgets_alert_test.cpp
    time_budgets_check_test.cpp
    time_budgets_declaration_test.cpp
    utf8_test.cpp
    utf8_view_test.cpp
    utf8_property_test.cpp
    version_test.cpp
)

# POSIX filesystem backend contract tests (F3.5): the real backend exists on
# POSIX hosts only, so the contract tests compile there; on Windows the backend
# is an UNSUPPORTED stub and the stub contract test covers it instead (rule
# 04: an error branch declared without a test is a known defect).
if(UNIX OR APPLE)
    target_sources(infinity_core_tests PRIVATE
        fs_os_test.cpp
        fs_os_tree_test.cpp
        fs_os_fault_test.cpp
    )
elseif(WIN32)
    target_sources(infinity_core_tests PRIVATE fs_os_stub_test.cpp)
endif()

find_package(Threads REQUIRED)
target_link_libraries(infinity_core_tests PRIVATE infinity_core Threads::Threads)

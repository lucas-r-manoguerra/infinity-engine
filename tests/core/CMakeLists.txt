infinity_add_test(infinity_core_tests
    main.cpp
    version_test.cpp
)

target_link_libraries(infinity_core_tests PRIVATE infinity_core)

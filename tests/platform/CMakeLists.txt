infinity_add_test(infinity_platform_tests
    main.cpp
    error_test.cpp
    input_test.cpp
    platform_test.cpp
    window_test.cpp
)

# The tests touch core types directly (Allocator, ErrorCategory), so core is
# linked explicitly even though infinity_platform already pulls it PUBLIC
# (mirror of tests/math).
target_link_libraries(infinity_platform_tests PRIVATE infinity::platform infinity_core)

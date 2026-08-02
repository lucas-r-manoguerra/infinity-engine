infinity_add_test(infinity_renderer_tests
    main.cpp
    error_test.cpp
    color_test.cpp
    color_lut_test.cpp
    render_target_test.cpp
    software_test.cpp
    camera_test.cpp
)

# The tests touch core types directly (Allocator, ErrorCategory), so core is
# linked explicitly even though infinity_renderer already pulls it PUBLIC
# (mirror of tests/math and tests/platform).
target_link_libraries(infinity_renderer_tests PRIVATE infinity::renderer infinity_core)

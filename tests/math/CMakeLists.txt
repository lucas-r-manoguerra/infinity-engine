infinity_add_test(infinity_math_tests
    main.cpp
    vec2_test.cpp
    vec3_test.cpp
    vec4_test.cpp
    mat4_inverse_test.cpp
    mat4_projection_test.cpp
    mat4_transform_test.cpp
    quat_basic_test.cpp
    quat_matrix_test.cpp
    quat_slerp_test.cpp
    transform_test.cpp
    mat4_property_test.cpp
    quat_property_test.cpp
    vec3_property_test.cpp
    transform_property_test.cpp
)

target_link_libraries(infinity_math_tests PRIVATE infinity::math infinity_core)

add_library(infinity_core STATIC
    src/arena_allocator.cpp
    src/diagnostics.cpp
    src/fs_in_memory.cpp
    src/loop.cpp
    src/pool_allocator.cpp
    src/thread_pool.cpp
    src/time.cpp
    src/utf8.cpp
    src/version.cpp
)

target_include_directories(infinity_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# thread_pool.cpp uses std::thread/std::condition_variable. PUBLIC so the
# pthread symbols the static library needs are resolved at every final link.
find_package(Threads REQUIRED)
target_link_libraries(infinity_core PUBLIC Threads::Threads)

add_library(infinity::core ALIAS infinity_core)

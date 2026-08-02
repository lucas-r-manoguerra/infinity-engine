# engine/ aggregates the module libraries. Dependency direction (rule 01):
# each module links only lower layers; CMake enforces it at link time.
add_subdirectory(core)
add_subdirectory(math)
add_subdirectory(platform)
add_subdirectory(renderer)

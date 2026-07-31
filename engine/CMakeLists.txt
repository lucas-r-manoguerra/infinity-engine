# engine/ aggregates the module libraries. Dependency direction (rule 01):
# each module links only lower layers; CMake enforces it at link time.
add_subdirectory(core)

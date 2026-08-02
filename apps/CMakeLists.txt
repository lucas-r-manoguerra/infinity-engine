# apps/ hosts the standalone executables built on top of the engine libraries
# (rule 01). Today apps/ links against infinity_core/math/platform/renderer:
# the bench harness ships since F1; apps/sandbox and infinity_runtime arrive
# in F6 (ROADMAP).
add_subdirectory(bench)

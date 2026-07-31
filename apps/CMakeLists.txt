# apps/ hosts the standalone executables built on top of the engine libraries
# (rule 01: apps/ depends on infinity_runtime and below). F1 currently ships
# the math-core benchmark harness; apps/sandbox lands in F6 (ROADMAP).
add_subdirectory(bench)

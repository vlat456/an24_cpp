# Shared signal allocation library (used by jit_solver and an24_codegen)
add_library(signal_allocation STATIC
    signal_allocation.cpp
)
target_include_directories(signal_allocation PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

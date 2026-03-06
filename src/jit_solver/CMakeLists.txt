add_library(jit_solver
    component.cpp
    systems.cpp
    state.cpp
    push_solver.cpp
)

target_include_directories(jit_solver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jit_solver PUBLIC
    spdlog::spdlog
)

add_library(jit_solver
    jit_solver.cpp
    component.cpp
    systems.cpp
    state.cpp
    simulator.cpp
    components/all.cpp
)

target_include_directories(jit_solver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(jit_solver PUBLIC
    spdlog::spdlog
)

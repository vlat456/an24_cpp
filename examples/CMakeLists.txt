add_library(an24_codegen STATIC
    ${CMAKE_SOURCE_DIR}/src/core/solvers/aot/codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/core/solvers/aot/codegen_header.cpp
    ${CMAKE_SOURCE_DIR}/src/core/solvers/aot/codegen_source.cpp
    ${CMAKE_SOURCE_DIR}/src/core/solvers/aot/codegen_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/core/solvers/aot/codegen_composite.cpp
    ${CMAKE_SOURCE_DIR}/src/core/solvers/aot/electrical_codegen.cpp
)
target_include_directories(an24_codegen PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(an24_codegen PRIVATE spdlog::spdlog json_io blueprint_v2)

# Re-enabled after ComponentVariant integration
find_package(OpenGL REQUIRED)

# Editor source lists — single source of truth in cmake/editor_sources.cmake
include(${CMAKE_SOURCE_DIR}/cmake/editor_sources.cmake)

add_executable(an24_editor
    ${CMAKE_SOURCE_DIR}/examples/an24_editor.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/app/editor_app.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/pi_zn_tuner.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/pi_zn_math.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/editor_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_core.cpp
    ${EDITOR_ALL_TESTABLE_SOURCES}
    ${EDITOR_IMGUI_SHELL_SOURCES}
    ${IMGUI_SOURCES}
)
target_include_directories(an24_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
    ${nfde_SOURCE_DIR}/src/include
)
target_link_libraries(an24_editor PRIVATE
    SDL2::SDL2
    "-framework OpenGL"
    "-framework Foundation"
    "-framework AppKit"
    "-framework UniformTypeIdentifiers"
    nfd
    jit_solver
    editor_semantic_canvas
    nlohmann_json::nlohmann_json
)

# Headless blueprint simulator with signal probing
add_executable(sim_debug sim_debug.cpp)
target_include_directories(sim_debug PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(sim_debug PRIVATE
    jit_solver
    blueprint_v2
    json_io
    nlohmann_json::nlohmann_json
)

# Temporary strict blueprint validation debugger
add_executable(bp2_debug_validate bp2_debug_validate.cpp)
target_include_directories(bp2_debug_validate PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(bp2_debug_validate PRIVATE
    blueprint_v2
    json_io
    nlohmann_json::nlohmann_json
)

# Benchmark JIT vs AOT
add_executable(benchmark_jit_vs_aot benchmark_jit_vs_aot.cpp)
target_include_directories(benchmark_jit_vs_aot PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(benchmark_jit_vs_aot PRIVATE
    jit_solver
    blueprint_v2
    json_io
)

# Push scheduler benchmark (single-pass)
add_executable(benchmark_push_scheduler benchmark_push_scheduler.cpp)
target_include_directories(benchmark_push_scheduler PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(benchmark_push_scheduler PRIVATE
    jit_solver
    json_io
)

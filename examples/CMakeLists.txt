add_executable(hello_world main.cpp)
target_include_directories(hello_world PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hello_world PRIVATE jit_solver spdlog::spdlog)

# Disabled for AOT-only refactor - Systems removed
# add_executable(components_test components_test.cpp)
# target_include_directories(components_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
# target_link_libraries(components_test PRIVATE jit_solver spdlog::spdlog)

# Disabled for AOT-only refactor - Systems removed
# add_executable(debug_test debug.cpp)
# target_include_directories(debug_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
# target_link_libraries(debug_test PRIVATE jit_solver spdlog::spdlog)

# Disabled for AOT-only refactor - Systems removed
# add_executable(json_test json_test.cpp)
# target_include_directories(json_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
# target_link_libraries(json_test PRIVATE json_parser jit_solver spdlog::spdlog)

# Disabled for AOT-only refactor - Systems removed
# add_executable(benchmark benchmark.cpp)
# target_include_directories(benchmark PRIVATE ${CMAKE_SOURCE_DIR}/src)
# target_link_libraries(benchmark PRIVATE json_parser jit_solver spdlog::spdlog)

add_library(an24_codegen STATIC
    ${CMAKE_SOURCE_DIR}/src/codegen/codegen.cpp
)
target_include_directories(an24_codegen PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)

# Disabled for AOT-only refactor - codegen needs update for template components
# add_executable(codegen_test codegen_test.cpp)
# target_include_directories(codegen_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
# target_link_libraries(codegen_test PRIVATE an24_codegen json_parser jit_solver spdlog::spdlog)

# Disabled for AOT-only refactor - needs template component instantiation
# add_executable(test_generated
#     ${CMAKE_SOURCE_DIR}/generated/test_generated.cpp
#     ${CMAKE_SOURCE_DIR}/generated/generated_an24_composite_test.cpp
# )
# target_include_directories(test_generated PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
# target_link_libraries(test_generated PRIVATE jit_solver spdlog::spdlog)

# Disabled for AOT-only refactor
# add_executable(aot_test
#     ${CMAKE_SOURCE_DIR}/generated/aot_test.cpp
#     ${CMAKE_SOURCE_DIR}/generated/generated_an24_composite_test.cpp
# )
# target_include_directories(aot_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
# target_link_libraries(aot_test PRIVATE jit_solver spdlog::spdlog)

# AOT compiled VSU test
# Disabled for AOT-only refactor
# add_executable(vsu_aot_test
#     ${CMAKE_SOURCE_DIR}/generated/test_vsu_aot.cpp
#     ${CMAKE_SOURCE_DIR}/generated/generated_vsu_test.cpp
# )
# target_include_directories(vsu_aot_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
# target_link_libraries(vsu_aot_test PRIVATE jit_solver spdlog::spdlog)

# AOT compiled GS24 test
# add_executable(gs24_aot_test
#     ${CMAKE_SOURCE_DIR}/generated/test_gs24_aot.cpp
#     ${CMAKE_SOURCE_DIR}/generated/generated_gs24_test.cpp
# )
# target_include_directories(gs24_aot_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
# target_link_libraries(gs24_aot_test PRIVATE jit_solver spdlog::spdlog)

# AOT benchmark
# Disabled for AOT-only refactor
# add_executable(bench_vsu_aot
#     ${CMAKE_SOURCE_DIR}/generated/bench_vsu_aot.cpp
#     ${CMAKE_SOURCE_DIR}/generated/generated_vsu_test.cpp
# )
# target_include_directories(bench_vsu_aot PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
# target_link_libraries(bench_vsu_aot PRIVATE jit_solver spdlog::spdlog)

# AOT benchmark with DMR
# Disabled for AOT-only refactor
# add_executable(bench_vsu_dmr_aot
#     ${CMAKE_SOURCE_DIR}/generated/bench_vsu_dmr_aot.cpp
#     ${CMAKE_SOURCE_DIR}/generated/generated_vsu_dmr_test.cpp
# )
# target_include_directories(bench_vsu_dmr_aot PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
# target_link_libraries(bench_vsu_dmr_aot PRIVATE jit_solver spdlog::spdlog)

# Disabled for AOT-only refactor
# add_executable(test_vsu_dmr_aot
#     ${CMAKE_SOURCE_DIR}/generated/test_vsu_dmr_aot.cpp
#     ${CMAKE_SOURCE_DIR}/generated/generated_vsu_dmr_test.cpp
# )
# target_include_directories(test_vsu_dmr_aot PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
# target_link_libraries(test_vsu_dmr_aot PRIVATE jit_solver spdlog::spdlog)

# add_executable(profile_vsu_dmr_aot
#     ${CMAKE_SOURCE_DIR}/generated/profile_vsu_dmr_aot.cpp
#     ${CMAKE_SOURCE_DIR}/generated/generated_vsu_dmr_test.cpp
# )
# target_include_directories(profile_vsu_dmr_aot PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
# target_link_libraries(profile_vsu_dmr_aot PRIVATE jit_solver spdlog::spdlog)

# Disabled for AOT-only refactor
# find_package(OpenGL REQUIRED)
# 
# add_executable(an24_editor
#     ${CMAKE_SOURCE_DIR}/examples/an24_editor.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/app.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/hittest.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/render.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
#     ${imgui_SOURCE_DIR}/imgui.cpp
#     ${imgui_SOURCE_DIR}/imgui_draw.cpp
#     ${imgui_SOURCE_DIR}/imgui_tables.cpp
#     ${imgui_SOURCE_DIR}/imgui_widgets.cpp
#     ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
#     ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
# )
# target_include_directories(an24_editor PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_SOURCE_DIR}/src/editor
#     ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
#     ${imgui_SOURCE_DIR}
#     ${imgui_SOURCE_DIR}/backends
#     ${SDL2_INCLUDE_DIRS}
#     ${nfde_SOURCE_DIR}/src/include
# )
# target_link_libraries(an24_editor PRIVATE
#     SDL2::SDL2
#     "-framework OpenGL"
#     "-framework Foundation"
#     "-framework AppKit"
#     "-framework UniformTypeIdentifiers"
#     spdlog::spdlog
#     ${CMAKE_BINARY_DIR}/_deps/nfde-build/src/libnfd.a
#     jit_solver
#     json_parser
# )

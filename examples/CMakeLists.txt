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
target_link_libraries(an24_codegen PRIVATE spdlog::spdlog json_io signal_allocation blueprint_v2)

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
# Disabled until generated_vsu_test.* is regenerated to current phase scheduler API
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

# Re-enabled after ComponentVariant integration
find_package(OpenGL REQUIRED)

add_executable(an24_editor
    ${CMAKE_SOURCE_DIR}/examples/an24_editor.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/app/editor_app.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_export.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_history.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_components.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_io.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/embedded_path_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_windows.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/subwindow_open_target.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/signal_key_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window_system.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/oscilloscope.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/pi_zn_tuner.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/pi_zn_math.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/editor_settings.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
      ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_down.cpp
      ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_drag.cpp
      ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_up.cpp
       ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_wires.cpp
       ${CMAKE_SOURCE_DIR}/src/editor/input/editing_host.cpp
       ${CMAKE_SOURCE_DIR}/src/editor/window/blueprint_window.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/canvas_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/oscilloscope_plot.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/layout/splitter.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/panels/inspector_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/panels/document_area.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/tabs/document_tabs.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/menu/main_menu.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/dialogs/file_dialogs.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/windows/sub_window_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/windows/oscilloscope_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/popups/context_menus.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/popups/color_picker_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/workspace_session_persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_core.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_render.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_common.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_analysis.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_build.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/imgui_theme.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
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

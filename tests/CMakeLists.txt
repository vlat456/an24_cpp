# JIT Solver tests - Disabled (uses old Systems API, needs update to ComponentVariant)
# add_executable(core_tests
#     core_test.cpp
# )
# target_include_directories(core_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
# target_link_libraries(core_tests PRIVATE
#     core
#     GTest::gtest_main
# )
# include(GoogleTest)
# gtest_discover_tests(core_tests)

include(GoogleTest)

add_executable(push_state_tests
    test_push_state.cpp
)
target_include_directories(push_state_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(push_state_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(push_state_tests)

add_executable(push_scheduler_tests
    test_push_scheduler.cpp
)
target_include_directories(push_scheduler_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(push_scheduler_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(push_scheduler_tests)

add_executable(push_build_validation_tests
    test_push_build_validation.cpp
)
target_include_directories(push_build_validation_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(push_build_validation_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(push_build_validation_tests)

add_executable(push_runtime_regression_tests
    test_push_runtime_regression.cpp
)
target_include_directories(push_runtime_regression_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(push_runtime_regression_tests PRIVATE
    blueprint_v2
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(push_runtime_regression_tests)

# Electrical island build tests (Batch 2: build-time electrical extraction)
add_executable(electrical_island_build_tests
    test_electrical_island_build.cpp
)
target_include_directories(electrical_island_build_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(electrical_island_build_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(electrical_island_build_tests
    PROPERTIES LABELS "raw_builder;electrical"
)

# Electrical primitive handle tests (Batch 3: handle assignment for wrapper components)
add_executable(electrical_handle_build_tests
    test_electrical_handles_build.cpp
)
target_include_directories(electrical_handle_build_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(electrical_handle_build_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(electrical_handle_build_tests
    PROPERTIES LABELS "raw_builder;electrical"
)

# Electrical subsolver tests (Batch 4: actual island solving)
add_executable(electrical_subsolver_tests
    test_electrical_subsolver.cpp
)
target_include_directories(electrical_subsolver_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(electrical_subsolver_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(electrical_subsolver_tests)

# Architecture regression tests (E-001, E-002, E-003, E-004)
add_executable(architecture_regression_tests
    test_architecture_regression.cpp
)
target_include_directories(architecture_regression_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(architecture_regression_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(architecture_regression_tests)

# Migrated tests - now using push-state API (SimulationState.values)
add_executable(pid_tests
    test_pid.cpp
)
target_include_directories(pid_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(pid_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(pid_tests)

add_executable(pd_tests
    test_pd.cpp
)
target_include_directories(pd_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(pd_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(pd_tests)

add_executable(subtract_tests
    test_subtract.cpp
)
target_include_directories(subtract_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(subtract_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(subtract_tests)

add_executable(logic_gates_tests
    test_logic_gates.cpp
)
target_include_directories(logic_gates_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(logic_gates_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(logic_gates_tests)

add_executable(arithmetic_tests
    test_arithmetic.cpp
)
target_include_directories(arithmetic_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(arithmetic_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(arithmetic_tests)

# Splitter component tests (1-to-2 signal fan-out + FirstOrderLag regression)
add_executable(splitter_tests
    test_splitter.cpp
)
target_include_directories(splitter_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(splitter_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(splitter_tests)

# AZS (Circuit Breaker) component tests
add_executable(azs_tests
    test_azs.cpp
)
target_include_directories(azs_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(azs_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(azs_tests)

# Logical solver tests (TDD - Comparator component)
add_executable(logical_solver_tests
    test_logical_solver.cpp
)
target_include_directories(logical_solver_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(logical_solver_tests PRIVATE
    jit_solver
    json_io
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(logical_solver_tests)

# Bridge lowering tests (issue #28)
add_executable(bridge_lowering_tests
    test_bridge_lowering.cpp
)
target_include_directories(bridge_lowering_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(bridge_lowering_tests PRIVATE
    an24_codegen
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(bridge_lowering_tests)

# CurrentSense (ammeter) component tests
add_executable(current_sense_tests
    test_current_sense.cpp
)
target_include_directories(current_sense_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(current_sense_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(current_sense_tests)

# Bug-hunt regression tests — AotProvider sentinel, transformer physics
add_executable(bug_hunt_regression_tests
    test_bug_hunt_regression.cpp
)
target_include_directories(bug_hunt_regression_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(bug_hunt_regression_tests PRIVATE
    json_io
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(bug_hunt_regression_tests)

# Migrated tests - now using push-state API (SimulationState.values)
add_executable(slew_rate_tests
    test_slew_rate.cpp
)
target_include_directories(slew_rate_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(slew_rate_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(slew_rate_tests)

add_executable(lut_tests
    test_lut.cpp
)
target_include_directories(lut_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(lut_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(lut_tests)

add_executable(time_delay_tests
    test_time_delay.cpp
)
target_include_directories(time_delay_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(time_delay_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(time_delay_tests)

add_executable(monostable_tests
    test_monostable.cpp
)
target_include_directories(monostable_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(monostable_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(monostable_tests)

add_executable(sample_hold_tests
    test_sample_hold.cpp
)
target_include_directories(sample_hold_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(sample_hold_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(sample_hold_tests)

# UI Math tests (Phase 1.1 of UI Library Extraction)
add_executable(ui_math_tests
    test_ui_math.cpp
)
target_include_directories(ui_math_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_math_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_math_tests)

# UI Layout tests (Phase 1.2 of UI Library Extraction)
add_executable(ui_layout_tests
    test_ui_layout.cpp
)
target_include_directories(ui_layout_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_layout_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_layout_tests)

# UI Renderer tests (Phase 1.3 of UI Library Extraction)
add_executable(ui_renderer_tests
    test_ui_renderer.cpp
)
target_include_directories(ui_renderer_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_renderer_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_renderer_tests)

# UI Widget purity tests (Phase 2.1 of UI Library Extraction)
add_executable(ui_widget_purity_tests
    test_ui_widget_purity.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/core/widget.cpp
)
target_include_directories(ui_widget_purity_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_widget_purity_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_widget_purity_tests)

# UI Scene tests (Phase 2.2 of UI Library Extraction)
add_executable(ui_scene_tests
    test_ui_scene.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/core/widget.cpp
)
target_include_directories(ui_scene_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_scene_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_scene_tests)

# UI Interning tests (InternedId + StringInterner)
add_executable(ui_interning_tests
    test_ui_interning.cpp
)
target_include_directories(ui_interning_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_interning_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_interning_tests)

# UI Grid tests (Phase 2.3 of UI Library Extraction)
add_executable(ui_grid_tests
    test_ui_grid.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/core/widget.cpp
)
target_include_directories(ui_grid_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_grid_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_grid_tests)

# UI RenderContext tests (Phase 3.2 of UI Library Extraction)
add_executable(ui_render_context_tests
    test_ui_render_context.cpp
)
target_include_directories(ui_render_context_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_render_context_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_render_context_tests)

# UI LinearLayout tests (Phase 4.1 of UI Library Extraction)
add_executable(ui_linear_layout_tests
    test_ui_linear_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/core/widget.cpp
)
target_include_directories(ui_linear_layout_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui_linear_layout_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(ui_linear_layout_tests)

# Visual Widget tests (Phase 1 of Scene Graph Refactoring)
add_executable(visual_widget_tests
    test_visual_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
)
target_include_directories(visual_widget_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(visual_widget_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(visual_widget_tests)

# Visual Grid tests (Phase 2 of Scene Graph Refactoring)
add_executable(visual_grid_tests
    test_visual_grid.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
)
target_include_directories(visual_grid_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(visual_grid_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(visual_grid_tests)

# Visual Scene tests (Phase 3 of Scene Graph Refactoring)
add_executable(visual_scene_v2_tests
    test_visual_scene_v2.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
)
target_include_directories(visual_scene_v2_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(visual_scene_v2_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(visual_scene_v2_tests)

# Visual Wire tests (Phase 6: Wire as scene-graph widget)
add_executable(visual_wire_tests
    test_visual_wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
)
target_include_directories(visual_wire_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(visual_wire_tests PRIVATE
    json_io
    GTest::gtest_main
)
gtest_discover_tests(visual_wire_tests)

# Visual Container tests (Phase 2 of Scene Graph Refactoring - containers)
add_executable(visual_container_tests
    test_visual_container.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
)
target_include_directories(visual_container_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(visual_container_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(visual_container_tests)

# Visual Primitives tests (Phase 3 - leaf widgets)
add_executable(visual_primitives_tests
    test_visual_primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
)
target_include_directories(visual_primitives_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(visual_primitives_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(visual_primitives_tests)

# Node slot layout engine tests (shell measure/arrange + presentation layout)
add_executable(node_slot_layout_tests
    test_node_slot_layout.cpp
)
target_include_directories(node_slot_layout_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(node_slot_layout_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(node_slot_layout_tests)

# Semantic scene snapshot tests (snapshot builder, node indexing, render/hit object projection)
add_executable(semantic_scene_snapshot_tests
    test_semantic_scene_snapshot.cpp
)
target_include_directories(semantic_scene_snapshot_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(semantic_scene_snapshot_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(semantic_scene_snapshot_tests)

# Presentation compiler tests (classify_frame_kind, default_content_presenter, compile pipeline)
add_executable(presentation_compiler_tests
    test_presentation_compiler.cpp
)
target_include_directories(presentation_compiler_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(presentation_compiler_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(presentation_compiler_tests)

# Semantic scene hit-test tests (point-in-region, layer ordering, interaction bindings)
add_executable(semantic_scene_hittest_tests
    test_semantic_scene_hittest.cpp
)
target_include_directories(semantic_scene_hittest_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(semantic_scene_hittest_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(semantic_scene_hittest_tests)

# Scene mutations tests (Phase 10: Blueprint <-> visual::Scene sync)
add_executable(scene_mutations_tests
    test_scene_mutations.cpp
    blueprint_v2/test_embedded_subwindow_scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
)
target_include_directories(scene_mutations_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(scene_mutations_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(scene_mutations_tests)

add_executable(document_safety_tests
    blueprint_v2/test_document_add_component_safety.cpp
    test_editor_ownership_isolation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_components.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_export.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_history.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_layout.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/document_io.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/embedded_path_utils.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/document_simulation.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/document_windows.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/window_system.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/oscilloscope.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_down.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_drag.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_up.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_wires.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/editing_host.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/window/blueprint_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/subwindow_open_target.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/signal_key_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/workspace_session_persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_analysis.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_bridge.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_build.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_common.cpp
)
target_compile_definitions(document_safety_tests PRIVATE EDITOR_TESTING)
target_include_directories(document_safety_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(document_safety_tests PRIVATE
    editor_semantic_canvas
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(document_safety_tests)

# Canvas input interaction regression tests (bus wire create/reconnect/swap)
add_executable(canvas_input_tests
    test_canvas_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_down.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_drag.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_up.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_wires.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/editing_host.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/window/blueprint_window.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/embedded_path_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
)
target_compile_definitions(canvas_input_tests PRIVATE EDITOR_TESTING)
target_include_directories(canvas_input_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(canvas_input_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(canvas_input_tests)

add_executable(window_invariants_tests
    test_window_invariants.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_down.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_drag.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_mouse_up.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input_wires.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/editing_host.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/window/blueprint_window.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/embedded_path_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
)
target_compile_definitions(window_invariants_tests PRIVATE EDITOR_TESTING)
target_include_directories(window_invariants_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(window_invariants_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(window_invariants_tests)

add_executable(oscilloscope_stats_tests
    test_oscilloscope_stats.cpp
)
target_include_directories(oscilloscope_stats_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(oscilloscope_stats_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(oscilloscope_stats_tests)

add_executable(canvas_scene_snapshot_tests
    test_canvas_scene_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/canvas_scene_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
)
target_compile_definitions(canvas_scene_snapshot_tests PRIVATE EDITOR_TESTING)
target_include_directories(canvas_scene_snapshot_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(canvas_scene_snapshot_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(canvas_scene_snapshot_tests)

add_executable(pi_zn_tuner_tests
    test_pi_zn_tuner.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/pi_zn_math.cpp
)
target_include_directories(pi_zn_tuner_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(pi_zn_tuner_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(pi_zn_tuner_tests)

# Port registry tests (TDD for codegen)
add_executable(port_registry_tests
    port_registry_test.cpp
)
target_include_directories(port_registry_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_compile_definitions(port_registry_tests PRIVATE
    TEST_DATA_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(port_registry_tests PRIVATE
    json_io
    GTest::gtest_main
)
gtest_discover_tests(port_registry_tests)

# Port macro tests (TDD for PORTS macro)
add_executable(port_macro_tests
    port_macro_test.cpp
)
target_include_directories(port_macro_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_compile_definitions(port_macro_tests PRIVATE
    TEST_DATA_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(port_macro_tests PRIVATE
    json_io
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(port_macro_tests)

# Editor ComponentVariant tests - Integration tests for Editor with ComponentVariant
add_executable(editor_componentvariant_tests
    editor_componentvariant_test.cpp
)
target_include_directories(editor_componentvariant_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_componentvariant_tests PRIVATE
    json_io
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(editor_componentvariant_tests)

add_executable(editor_settings_tests
    test_editor_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/editor_settings.cpp
)
target_include_directories(editor_settings_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_settings_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_settings_tests)

# Port Layout Resolver tests (Phase 2: Resolution Algorithm)
add_executable(port_layout_resolver_tests
    test_port_layout_resolver.cpp
)
target_include_directories(port_layout_resolver_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/ui
    ${CMAKE_SOURCE_DIR}/src/ui/core
)
target_link_libraries(port_layout_resolver_tests PRIVATE
    json_io
    GTest::gtest_main
)
gtest_discover_tests(port_layout_resolver_tests)


# Editor viewport tests (TDD Step 3)
add_executable(editor_viewport_tests
    test_viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
)
target_include_directories(editor_viewport_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(editor_viewport_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_viewport_tests)

# Editor interaction tests (TDD Step 4) - Disabled for AOT-only refactor
# add_executable(editor_interaction_tests
#     test_interaction.cpp
# )
# target_include_directories(editor_interaction_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_SOURCE_DIR}/src/editor
# )
# target_link_libraries(editor_interaction_tests PRIVATE
#     GTest::gtest_main
# )
# gtest_discover_tests(editor_interaction_tests)

# Editor render tests


# Editor event tests (TDD Step 7) - Disabled for AOT-only refactor
# add_executable(editor_event_tests
#     test_events.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/app.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/hittest.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
# )
# target_include_directories(editor_event_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_SOURCE_DIR}/src/editor
#     ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
# )
# target_link_libraries(editor_event_tests PRIVATE
#     core
#     json_io
#     GTest::gtest_main
# )
# gtest_discover_tests(editor_event_tests)

# GL setup configuration tests (TDD Step 9)
# add_executable(editor_gl_setup_tests
#     test_gl_setup.cpp
# )
# target_include_directories(editor_gl_setup_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_SOURCE_DIR}/src/editor
# )
# target_link_libraries(editor_gl_setup_tests PRIVATE
#     GTest::gtest_main
# )
# gtest_discover_tests(editor_gl_setup_tests)

add_executable(json_parser_tests
    json_parser_test.cpp
)
target_include_directories(json_parser_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(json_parser_tests PRIVATE
    json_io
    GTest::gtest_main
)
gtest_discover_tests(json_parser_tests)

# Editor routing tests (TDD - routing points)
# add_executable(editor_routing_tests
#     test_routing.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
# )
# target_include_directories(editor_routing_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_SOURCE_DIR}/src/editor
# )
# target_link_libraries(editor_routing_tests PRIVATE
#     GTest::gtest_main
# )
# gtest_discover_tests(editor_routing_tests)

# A* Router tests


# Factory validation tests
add_executable(factory_validation_tests
    factory_validation_test.cpp
)
target_include_directories(factory_validation_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(factory_validation_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(factory_validation_tests)

# Blueprint integration tests
add_executable(blueprint_integration_tests
    test_blueprint_integration.cpp
)
target_include_directories(blueprint_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(blueprint_integration_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(blueprint_integration_tests)

# Blueprint loading tests (Phase 2: fallback to blueprints/)
add_executable(blueprint_loading_tests
    test_blueprint_loading.cpp
)
target_include_directories(blueprint_loading_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(blueprint_loading_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(blueprint_loading_tests)

# Provider pattern tests (AOT vs JIT unified components)
add_executable(provider_pattern_tests
    provider_test.cpp
)
target_include_directories(provider_pattern_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(provider_pattern_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(provider_pattern_tests)

# AsymTMO and FastTMO tests
add_executable(asym_tmo_tests
    test_asym_tmo.cpp
)
target_include_directories(asym_tmo_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(asym_tmo_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(asym_tmo_tests)

# LUT codegen tests (AOT code generation for LUT arena)
add_executable(lut_codegen_tests
    test_lut_codegen.cpp
)
target_include_directories(lut_codegen_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(lut_codegen_tests PRIVATE
    an24_codegen
    json_io
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(lut_codegen_tests)

# Port mapping regression tests (catches missing ports in string_to_port_name)
add_executable(port_map_regression_tests
    test_port_map_regression.cpp
)
target_include_directories(port_map_regression_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(port_map_regression_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(port_map_regression_tests)

# AND gate debug integration test
add_executable(and_gate_debug_tests
    test_and_gate_debug.cpp
)
target_include_directories(and_gate_debug_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(and_gate_debug_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(and_gate_debug_tests)

# Codegen identifier sanitization regression tests
add_executable(codegen_sanitize_tests
    test_codegen_sanitize.cpp
)
target_include_directories(codegen_sanitize_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(codegen_sanitize_tests PRIVATE
    an24_codegen
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(codegen_sanitize_tests)

# V-to-Bool converter tests
add_executable(v_to_bool_tests
    test_v_to_bool.cpp
)
target_include_directories(v_to_bool_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(v_to_bool_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(v_to_bool_tests)

# PI and P controller tests
add_executable(pi_p_tests
    test_pi_p.cpp
)
target_include_directories(pi_p_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(pi_p_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(pi_p_tests)

# Inspector tests
add_executable(inspector_tests
    test_inspector.cpp
    ../src/editor/visual/inspector/inspector_core.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
)
target_compile_definitions(inspector_tests PRIVATE EDITOR_TESTING)
target_include_directories(inspector_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(inspector_tests PRIVATE
    blueprint_v2
    json_io
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(inspector_tests)

# Editor hierarchical blueprint tests (TDD - collapsed node rendering, navigation, persistence)

# VisualPort tests (TDD - OOP port refactor)


# Refactoring regression tests (audit: extracted types, [[nodiscard]], dependency decoupling)


# Properties window tests (Phase 2: PropertiesWindow logic)
add_executable(properties_window_tests
    test_properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/input/editing_host.cpp
     ${CMAKE_SOURCE_DIR}/src/editor/embedded_path_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/commands.cpp
)
target_compile_definitions(properties_window_tests PRIVATE EDITOR_TESTING)
target_include_directories(properties_window_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/ui
    ${CMAKE_SOURCE_DIR}/src/ui/core
    ${CMAKE_SOURCE_DIR}/src/ui/math
    ${CMAKE_SOURCE_DIR}/src/ui/layout
    ${CMAKE_SOURCE_DIR}/src/ui/renderer
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(properties_window_tests PRIVATE
    blueprint_v2
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(properties_window_tests)

# Codegen accumulator regression tests — verify AOT emits accumulator-based domain scheduling
add_executable(codegen_accumulator_tests
    test_codegen_accumulator.cpp
)
target_include_directories(codegen_accumulator_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(codegen_accumulator_tests PRIVATE
    an24_codegen
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(codegen_accumulator_tests)

# AOT Composite codegen tests (Phase 7: hierarchical AOT codegen)
add_executable(aot_composite_tests
    test_aot_composite.cpp
)
target_include_directories(aot_composite_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(aot_composite_tests PRIVATE
    an24_codegen
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(aot_composite_tests
    PROPERTIES LABELS "production_path;electrical"
)

# Minimal bridge-based JIT vs AOT equivalence regression
add_executable(jit_aot_bridge_equivalence_tests
    test_jit_aot_bridge_equivalence.cpp
)
target_include_directories(jit_aot_bridge_equivalence_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(jit_aot_bridge_equivalence_tests PRIVATE
    an24_codegen
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(jit_aot_bridge_equivalence_tests
    PROPERTIES LABELS "production_path;electrical"
)

add_executable(production_path_parity_tests
    test_production_path_parity.cpp
)
target_include_directories(production_path_parity_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(production_path_parity_tests PRIVATE
    an24_codegen
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(production_path_parity_tests
    PROPERTIES LABELS "production_path;electrical"
)

add_executable(production_path_port_map_tests
    test_production_path_port_map.cpp
)
target_include_directories(production_path_port_map_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(production_path_port_map_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(production_path_port_map_tests
    PROPERTIES LABELS "production_path"
)

add_executable(production_path_push_runtime_tests
    test_production_path_push_runtime.cpp
)
target_include_directories(production_path_push_runtime_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(production_path_push_runtime_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(production_path_push_runtime_tests
    PROPERTIES LABELS "production_path"
)

# Node color tests (Phase 1: data model, persistence, visual node)

# Node auto-size tests (TDD - dynamic resize based on ports + content)


# =============================================================================
# New component tests (AsymSlewRate, TimeDelay, Monostable, SampleHold, Integrator)
# =============================================================================

# AsymSlewRate component tests
add_executable(asym_slew_rate_tests
    test_asym_slew_rate.cpp
)
target_include_directories(asym_slew_rate_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(asym_slew_rate_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(asym_slew_rate_tests)

# Integrator component tests
add_executable(integrator_tests
    test_integrator.cpp
)
target_include_directories(integrator_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(integrator_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(integrator_tests)

# Accumulator component tests
add_executable(accumulator_tests
    test_accumulator.cpp
)
target_include_directories(accumulator_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(accumulator_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(accumulator_tests)

# 12SAM28 composite battery tests
add_executable(sam28_tests
    test_12sam28.cpp
)
target_include_directories(sam28_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(sam28_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(sam28_tests)

# Clamp and Normalize component tests
add_executable(clamp_normalize_tests
    test_clamp_normalize.cpp
)
target_include_directories(clamp_normalize_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(clamp_normalize_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(clamp_normalize_tests)

# Comparison component tests (Min/Max/Greater/Lesser/GreaterEq/LesserEq)
add_executable(comparison_components_tests
    test_comparison_components.cpp
)
target_include_directories(comparison_components_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(comparison_components_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(comparison_components_tests)

# Spring component tests (mechanical domain)
add_executable(spring_tests
    test_spring.cpp
)
target_include_directories(spring_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(spring_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(spring_tests)

add_executable(v3_migration_tests
    test_v3_migration.cpp
    test_closed_circuit_strict_load.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_include_directories(v3_migration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(v3_migration_tests PRIVATE
    blueprint_v2
    json_io
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(v3_migration_tests)

# Document + WindowSystem tests (MDI lifecycle, regression tests)
# DISABLED: test_document_window_system.cpp was removed
# add_executable(document_window_system_tests
#     test_document_window_system.cpp
#     ...
# )


# Command system tests (Undo/Redo)
add_executable(command_tests
    test_commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_common.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_analysis.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_build.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_include_directories(command_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/ui
    ${CMAKE_SOURCE_DIR}/src/ui/core
    ${CMAKE_SOURCE_DIR}/src/ui/math
    ${CMAKE_SOURCE_DIR}/src/ui/layout
    ${CMAKE_SOURCE_DIR}/src/ui/renderer
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(command_tests PRIVATE
    blueprint_v2
    jit_solver
    json_io
    spdlog::spdlog
    GTest::gtest_main
)

add_executable(unmapped_port_regression_tests
    test_unmapped_port_regression.cpp
)
target_include_directories(unmapped_port_regression_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(unmapped_port_regression_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)

add_executable(bp2_path_tests
    blueprint_v2/test_path.cpp
)
target_include_directories(bp2_path_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bp2_path_tests PRIVATE
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(bp2_path_tests)

add_executable(bp2_interface_tests
    blueprint_v2/test_interface.cpp
)
target_include_directories(bp2_interface_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bp2_interface_tests PRIVATE
    blueprint_v2
    json_io
    GTest::gtest_main
)
gtest_discover_tests(bp2_interface_tests)

# Blueprint v2: Blueprint class tests (Phase 3)
add_executable(bp2_blueprint_tests
    blueprint_v2/test_blueprint.cpp
    test_issue_91_blueprint_instance_iface_authority.cpp
)
target_include_directories(bp2_blueprint_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(bp2_blueprint_tests PRIVATE
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(bp2_blueprint_tests)

# Codec v2 tests (Phase 5)
add_executable(bp2_codec_tests
    blueprint_v2/test_codec.cpp
    blueprint_v2/test_subwindow_open_target.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/subwindow_open_target.cpp
)
target_include_directories(bp2_codec_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(bp2_codec_tests PRIVATE
    blueprint_v2
    json_io
    GTest::gtest_main
)
gtest_discover_tests(bp2_codec_tests)

# Flattener v2 tests (Phase 6)
add_executable(bp2_flattener_tests
    blueprint_v2/test_flattener.cpp
)
target_include_directories(bp2_flattener_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bp2_flattener_tests PRIVATE
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(bp2_flattener_tests)

# EditorModel v2 tests (Phase 7)
add_executable(bp2_editor_model_tests
    blueprint_v2/test_editor_model.cpp
    blueprint_v2/test_embedded_editing_undo.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/subwindow_open_target.cpp
)
target_include_directories(bp2_editor_model_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bp2_editor_model_tests PRIVATE
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(bp2_editor_model_tests)

add_executable(bp2_bake_tests
    blueprint_v2/test_bake.cpp
)
target_include_directories(bp2_bake_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(bp2_bake_tests PRIVATE
    blueprint_v2
    json_io
    GTest::gtest_main
)
gtest_discover_tests(bp2_bake_tests)

# Validation tests (PathResolver, WireValidator, Blueprint invariants)
add_executable(bp2_validation_tests
    blueprint_v2/test_validation.cpp
    blueprint_v2/test_persist_validation.cpp
    blueprint_v2/test_issue_23_nested_inline_only.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_include_directories(bp2_validation_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(bp2_validation_tests PRIVATE
    blueprint_v2
    json_io
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(bp2_validation_tests)

# Electrical primitives tests (Step 13: explicit primitive electrical nodes)
add_executable(electrical_primitives_tests
    test_electrical_primitives.cpp
)
target_include_directories(electrical_primitives_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(electrical_primitives_tests PRIVATE
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(electrical_primitives_tests
    PROPERTIES LABELS "raw_builder;electrical"
)

# JIT↔AOT electrical parity fixtures (Phase 0-1: reference fixtures for semantic validation)
add_executable(electrical_parity_fixtures_tests
    test_electrical_parity_fixtures.cpp
)
target_include_directories(electrical_parity_fixtures_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(electrical_parity_fixtures_tests PRIVATE
    jit_solver
    json_io
    an24_codegen
    GTest::gtest_main
)
gtest_discover_tests(electrical_parity_fixtures_tests
    PROPERTIES LABELS "raw_builder;electrical"
)

# External reference signal mapping tests (parent-bound blueprint rendering)
add_executable(external_ref_signal_mapping_tests
    test_external_ref_signal_mapping.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/signal_key_resolver.cpp
)
target_include_directories(external_ref_signal_mapping_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(external_ref_signal_mapping_tests PRIVATE
    jit_solver
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(external_ref_signal_mapping_tests)

add_executable(signal_key_resolver_tests
    test_signal_key_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/signal_key_resolver.cpp
)
target_include_directories(signal_key_resolver_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(signal_key_resolver_tests PRIVATE
    jit_solver
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(signal_key_resolver_tests)

# KnobSwitch (multi-position rotary selector) component tests
add_executable(knob_switch_tests
    test_knob_switch.cpp
)
target_include_directories(knob_switch_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(knob_switch_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(knob_switch_tests)

# Export parity tests (Issue #38: unified elaboration via flattener)
add_executable(export_module_tests
    blueprint_v2/test_export_flattener_parity.cpp
)
target_include_directories(export_module_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(export_module_tests PRIVATE
    blueprint_v2
    json_io
    GTest::gtest_main
)
gtest_discover_tests(export_module_tests)

# Editor identity type-safety tests (Issue #45: NodeId typed wrapper)
add_executable(editor_identity_tests
    test_editor_identity.cpp
)
target_include_directories(editor_identity_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(editor_identity_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_identity_tests)

# LibraryIndex tests (Issue #85: dedicated library index)
add_executable(bp2_library_index_tests
    blueprint_v2/test_library_index.cpp
    blueprint_v2/test_type_def_to_blueprint.cpp
)
target_include_directories(bp2_library_index_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(bp2_library_index_tests PRIVATE
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(bp2_library_index_tests)

# elaborate_for_jit() vs legacy JSON path parity tests (Issue #113)
add_executable(elaborate_jit_parity_tests
    test_elaborate_jit_parity.cpp
)
target_include_directories(elaborate_jit_parity_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(elaborate_jit_parity_tests PRIVATE
    blueprint_v2
    jit_solver
    json_io
    GTest::gtest_main
)
gtest_discover_tests(elaborate_jit_parity_tests
    PROPERTIES LABELS "production_path"
)

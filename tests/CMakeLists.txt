include(GoogleTest)

# ==...== Helper Function ==...==

# add_sim_test: Creates a test executable with common boilerplate.
# Usage:
#   add_sim_test(name SOURCES src1.cpp src2.cpp
#                    LINKS lib1 lib2
#                    INCLUDES extra/inc/dir
#                    DEFINES SOME_DEF
#                    LABELS "label1;label2")
#
# Default include dirs: ${CMAKE_SOURCE_DIR}/src, ${CMAKE_BINARY_DIR}/_deps/json-src/include
# Default link: GTest::gtest_main (always appended)
# gtest_discover_tests() is always called (with LABELS if provided)
function(add_sim_test target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LINKS;INCLUDES;DEFINES;LABELS" ${ARGN})
    add_executable(${target_name} ${ARG_SOURCES})
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/_deps/json-src/include
        ${ARG_INCLUDES}
    )
    if(ARG_DEFINES)
        target_compile_definitions(${target_name} PRIVATE ${ARG_DEFINES})
    endif()
    target_link_libraries(${target_name} PRIVATE
        ${ARG_LINKS}
        GTest::gtest_main
    )
    if(ARG_LABELS)
        gtest_discover_tests(${target_name} PROPERTIES LABELS "${ARG_LABELS}")
    else()
        gtest_discover_tests(${target_name})
    endif()
endfunction()

# ==...== Shared Source Lists ==...==

# Editor source lists — single source of truth in cmake/editor_sources.cmake
include(${CMAKE_SOURCE_DIR}/cmake/editor_sources.cmake)

# UI widget sources — used by ui_*_tests that compile widget.cpp
set(UI_WIDGET_SOURCE
    ${CMAKE_SOURCE_DIR}/src/ui/core/widget.cpp
)

# ==...== Push Scheduler Tests ==...==

add_sim_test(push_state_tests SOURCES test_push_state.cpp LINKS jit_solver)

add_sim_test(push_scheduler_tests SOURCES test_push_scheduler.cpp LINKS jit_solver)

add_sim_test(push_build_validation_tests SOURCES test_push_build_validation.cpp LINKS jit_solver blueprint_v2)

add_sim_test(push_runtime_regression_tests SOURCES test_push_runtime_regression.cpp LINKS blueprint_v2 jit_solver)

# ==...== Electrical Build Tests ==...==

# Electrical island build tests (Batch 2: build-time electrical extraction)
add_sim_test(electrical_island_build_tests SOURCES test_electrical_island_build.cpp LINKS jit_solver json_io LABELS "raw_builder;electrical")

# Electrical primitive handle tests (Batch 3: handle assignment for wrapper components)
add_sim_test(electrical_handle_build_tests SOURCES test_electrical_handles_build.cpp LINKS jit_solver json_io LABELS "raw_builder;electrical")

# Electrical subsolver tests (Batch 4: actual island solving)
add_sim_test(electrical_subsolver_tests SOURCES test_electrical_subsolver.cpp LINKS jit_solver)

# Hydraulic subsolver tests (Batch 4: hydraulic domain nodal analysis)
add_sim_test(hydraulic_subsolver_tests SOURCES test_hydraulic_subsolver.cpp LINKS jit_solver)

# Hydraulic integration tests (Batch 5: end-to-end build + solve pipeline)
add_sim_test(hydraulic_integration_tests SOURCES test_hydraulic_integration.cpp LINKS jit_solver)

# ==...== Architecture Regression Tests ==...==

# Architecture regression tests (E-001, E-002, E-003, E-004)
add_sim_test(architecture_regression_tests SOURCES test_architecture_regression.cpp LINKS jit_solver json_io)

# Port queries self-containment test — verifies port_queries.h can be included alone
add_sim_test(port_queries_self_contained_tests SOURCES test_port_queries_self_contained.cpp LINKS jit_solver)

# ==...== Migrated Push-State API Tests ==...==

# Migrated tests - now using push-state API (SimulationState.values)
add_sim_test(pid_tests SOURCES test_pid.cpp LINKS jit_solver json_io)

add_sim_test(pd_tests SOURCES test_pd.cpp LINKS jit_solver json_io)

add_sim_test(subtract_tests SOURCES test_subtract.cpp LINKS jit_solver json_io)

add_sim_test(logic_gates_tests SOURCES test_logic_gates.cpp LINKS jit_solver json_io)

add_sim_test(arithmetic_tests SOURCES test_arithmetic.cpp LINKS jit_solver json_io)

# Splitter component tests (1-to-2 signal fan-out + FirstOrderLag regression)
add_sim_test(splitter_tests SOURCES test_splitter.cpp LINKS jit_solver json_io)

# ==...== AZS Tests ==...==

# AZS (Circuit Breaker) component tests
add_sim_test(azs_tests SOURCES test_azs.cpp LINKS jit_solver)

# ==...== Logical Solver Tests ==...==

# Logical solver tests (TDD - Comparator component)
add_sim_test(logical_solver_tests SOURCES test_logical_solver.cpp LINKS jit_solver json_io blueprint_v2 INCLUDES ${CMAKE_SOURCE_DIR}/src/editor)

# ==...== Bridge Lowering Tests ==...==

# Bridge lowering tests (issue #28)
add_sim_test(bridge_lowering_tests SOURCES test_bridge_lowering.cpp LINKS an24_codegen jit_solver json_io INCLUDES ${CMAKE_SOURCE_DIR}/src/core)

# ==...== Current Sense Tests ==...==

# CurrentSense (ammeter) component tests
add_sim_test(current_sense_tests SOURCES test_current_sense.cpp LINKS jit_solver json_io)

# ==...== Bug Hunt Regression Tests ==...==

# Bug-hunt regression tests — AotProvider sentinel, transformer physics
add_sim_test(bug_hunt_regression_tests SOURCES test_bug_hunt_regression.cpp LINKS json_io jit_solver)

# ==...== Slew Rate, LUT, Time Delay Tests ==...==

# Migrated tests - now using push-state API (SimulationState.values)
add_sim_test(slew_rate_tests SOURCES test_slew_rate.cpp LINKS jit_solver json_io)

add_sim_test(lut_tests SOURCES test_lut.cpp LINKS jit_solver json_io)

add_sim_test(time_delay_tests SOURCES test_time_delay.cpp LINKS jit_solver json_io)

add_sim_test(monostable_tests SOURCES test_monostable.cpp LINKS jit_solver json_io)

add_sim_test(sample_hold_tests SOURCES test_sample_hold.cpp LINKS jit_solver json_io)

# ==...== UI Library Tests ==...==

# UI Math tests (Phase 1.1 of UI Library Extraction)
add_sim_test(ui_math_tests SOURCES test_ui_math.cpp)

# UI Layout tests (Phase 1.2 of UI Library Extraction)
add_sim_test(ui_layout_tests SOURCES test_ui_layout.cpp)

# UI Renderer tests (Phase 1.3 of UI Library Extraction)
add_sim_test(ui_renderer_tests SOURCES test_ui_renderer.cpp)

# UI Widget purity tests (Phase 2.1 of UI Library Extraction)
add_sim_test(ui_widget_purity_tests SOURCES test_ui_widget_purity.cpp ${UI_WIDGET_SOURCE})

# UI Scene tests (Phase 2.2 of UI Library Extraction)
add_sim_test(ui_scene_tests SOURCES test_ui_scene.cpp ${UI_WIDGET_SOURCE})

# UI Interning tests (InternedId + StringInterner)
add_sim_test(ui_interning_tests SOURCES test_ui_interning.cpp)

# UI Grid tests (Phase 2.3 of UI Library Extraction)
add_sim_test(ui_grid_tests SOURCES test_ui_grid.cpp ${UI_WIDGET_SOURCE})

# UI RenderContext tests (Phase 3.2 of UI Library Extraction)
add_sim_test(ui_render_context_tests SOURCES test_ui_render_context.cpp)

# UI LinearLayout tests (Phase 4.1 of UI Library Extraction)
add_sim_test(ui_linear_layout_tests SOURCES test_ui_linear_layout.cpp ${UI_WIDGET_SOURCE})

# ==...== Visual Widget Tests ==...==

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

# ==...== Semantic Canvas Tests ==...==

# Node slot layout engine tests (shell measure/arrange + presentation layout)
add_sim_test(node_slot_layout_tests SOURCES test_node_slot_layout.cpp LINKS editor_semantic_canvas INCLUDES ${CMAKE_SOURCE_DIR}/src/editor)

# Semantic scene snapshot tests (snapshot builder, node indexing, render/hit object projection)
add_sim_test(semantic_scene_snapshot_tests SOURCES test_semantic_scene_snapshot.cpp LINKS editor_semantic_canvas INCLUDES ${CMAKE_SOURCE_DIR}/src/editor)

# Presentation compiler tests (classify_frame_kind, default_content_presenter, compile pipeline)
add_sim_test(presentation_compiler_tests SOURCES test_presentation_compiler.cpp LINKS editor_semantic_canvas INCLUDES ${CMAKE_SOURCE_DIR}/src/editor)

# Semantic scene hit-test tests (point-in-region, layer ordering, interaction bindings)
add_sim_test(semantic_scene_hittest_tests SOURCES test_semantic_scene_hittest.cpp LINKS editor_semantic_canvas INCLUDES ${CMAKE_SOURCE_DIR}/src/editor)

# ==...== Scene Mutations Tests ==...==

# Scene mutations tests (Phase 10: Blueprint <-> visual::Scene sync)
add_executable(scene_mutations_tests
    test_scene_mutations.cpp
    blueprint_v2/test_embedded_subwindow_scene.cpp
    ${EDITOR_VISUAL_FULL_SOURCES}
)
target_include_directories(scene_mutations_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(scene_mutations_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(scene_mutations_tests)

# ==...== Node Badge Unit Tests ==...==

# Pure unit tests for NodeBadgeSet, BadgeVisuals, codepoint_to_utf8, IconFont
add_sim_test(node_badge_tests SOURCES test_node_badge.cpp)

# ==...== Document Safety Tests ==...==

add_executable(document_safety_tests
    blueprint_v2/test_document_add_component_safety.cpp
    test_editor_ownership_isolation.cpp
    test_oscilloscope_model.cpp
    ${EDITOR_DOCUMENT_SOURCES}
    ${EDITOR_CANVAS_INPUT_SOURCES}
    ${EDITOR_VISUAL_FULL_SOURCES}
)
target_compile_definitions(document_safety_tests PRIVATE EDITOR_TESTING)
target_include_directories(document_safety_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(document_safety_tests PRIVATE
    editor_semantic_canvas
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(document_safety_tests)

# ==...== Canvas Input Tests ==...==

# Canvas input interaction regression tests (bus wire create/reconnect/swap)
add_executable(canvas_input_tests
    test_canvas_input.cpp
    ${EDITOR_CANVAS_INPUT_SOURCES}
    ${EDITOR_VISUAL_FULL_SOURCES}
)
target_compile_definitions(canvas_input_tests PRIVATE EDITOR_TESTING)
target_include_directories(canvas_input_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(canvas_input_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(canvas_input_tests)

# ==...== Window Invariants Tests ==...==

add_executable(window_invariants_tests
    test_window_invariants.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/focus_scope.cpp
    ${EDITOR_CANVAS_INPUT_SOURCES}
    ${EDITOR_VISUAL_FULL_SOURCES}
)
target_compile_definitions(window_invariants_tests PRIVATE EDITOR_TESTING)
target_include_directories(window_invariants_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(window_invariants_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(window_invariants_tests)

# ==...== Oscilloscope Stats Tests ==...==

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

# ==...== Auto Layout Tests ==...==

add_executable(auto_layout_tests
    test_auto_layout.cpp
)
target_include_directories(auto_layout_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(auto_layout_tests PRIVATE
    blueprint_v2
    core_registry
    GTest::gtest_main
)
gtest_discover_tests(auto_layout_tests)

# ==...== Canvas Scene Snapshot Tests ==...==

add_executable(canvas_scene_snapshot_tests
    test_canvas_scene_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/canvas_scene_snapshot.cpp
    ${EDITOR_VISUAL_FULL_SOURCES}
)
target_compile_definitions(canvas_scene_snapshot_tests PRIVATE EDITOR_TESTING)
target_include_directories(canvas_scene_snapshot_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(canvas_scene_snapshot_tests PRIVATE
    editor_semantic_canvas
    GTest::gtest_main
)
gtest_discover_tests(canvas_scene_snapshot_tests)

# ==...== PI ZN Tuner Tests ==...==

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

# ==...== Port Registry/Macro Tests ==...==

# Port registry tests (TDD for codegen)
add_sim_test(port_registry_tests SOURCES port_registry_test.cpp LINKS json_io)
target_compile_definitions(port_registry_tests PRIVATE TEST_DATA_DIR="${CMAKE_SOURCE_DIR}")

# Port macro tests (TDD for PORTS macro)
add_sim_test(port_macro_tests SOURCES port_macro_test.cpp LINKS json_io jit_solver)
target_compile_definitions(port_macro_tests PRIVATE TEST_DATA_DIR="${CMAKE_SOURCE_DIR}")

# ==...== Editor Component Variant Tests ==...==

# Editor ComponentVariant tests - Integration tests for Editor with ComponentVariant
add_sim_test(editor_componentvariant_tests SOURCES editor_componentvariant_test.cpp LINKS json_io jit_solver)

# ==...== Editor Settings Tests ==...==

add_sim_test(editor_settings_tests
    SOURCES test_editor_settings.cpp ${CMAKE_SOURCE_DIR}/src/editor/editor_settings.cpp
    INCLUDES ${CMAKE_SOURCE_DIR}/src/editor
)

# ==...== Port Layout Resolver Tests ==...==

# Port Layout Resolver tests (Phase 2: Resolution Algorithm)
add_sim_test(port_layout_resolver_tests SOURCES test_port_layout_resolver.cpp LINKS json_io INCLUDES ${CMAKE_SOURCE_DIR}/src/editor ${CMAKE_SOURCE_DIR}/src/ui ${CMAKE_SOURCE_DIR}/src/ui/core)

# ==...== Editor Viewport Tests ==...==

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

# ==...== JSON Parser Tests ==...==

add_executable(json_parser_tests
    json_parser_test.cpp
)
target_include_directories(json_parser_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(json_parser_tests PRIVATE
    json_io
    GTest::gtest_main
)
gtest_discover_tests(json_parser_tests)

# ==...== Factory Validation Tests ==...==

# Factory validation tests
add_sim_test(factory_validation_tests SOURCES factory_validation_test.cpp LINKS jit_solver json_io)

# ==...== Blueprint Integration Tests ==...==

# Blueprint integration tests
add_sim_test(blueprint_integration_tests SOURCES test_blueprint_integration.cpp LINKS jit_solver INCLUDES ${CMAKE_SOURCE_DIR}/src/core)

# Blueprint loading tests (Phase 2: fallback to blueprints/)
add_sim_test(blueprint_loading_tests SOURCES test_blueprint_loading.cpp LINKS jit_solver INCLUDES ${CMAKE_SOURCE_DIR}/src/core)

# ==...== Provider Pattern Tests ==...==

# Provider pattern tests (AOT vs JIT unified components)
add_sim_test(provider_pattern_tests SOURCES provider_test.cpp LINKS jit_solver)

# ==...== AsymTMO Tests ==...==

# AsymTMO and FastTMO tests
add_sim_test(asym_tmo_tests SOURCES test_asym_tmo.cpp LINKS jit_solver json_io)

# ==...== LUT Codegen Tests ==...==

# LUT codegen tests (AOT code generation for LUT arena)
add_sim_test(lut_codegen_tests SOURCES test_lut_codegen.cpp LINKS an24_codegen json_io jit_solver)

# ==...== Port Map Regression Tests ==...==

# Port mapping regression tests (catches missing ports in string_to_port_name)
add_sim_test(port_map_regression_tests SOURCES test_port_map_regression.cpp LINKS jit_solver json_io)

# ==...== AND Gate Debug Tests ==...==

# AND gate debug integration test
add_sim_test(and_gate_debug_tests SOURCES test_and_gate_debug.cpp LINKS jit_solver json_io)

# ==...== Codegen Sanitize Tests ==...==

# Codegen identifier sanitization regression tests
add_sim_test(codegen_sanitize_tests SOURCES test_codegen_sanitize.cpp LINKS an24_codegen jit_solver json_io)

# ==...== V to Bool Tests ==...==

# V-to-Bool converter tests
add_sim_test(v_to_bool_tests SOURCES test_v_to_bool.cpp LINKS jit_solver json_io)

# ==...== PI and P Controller Tests ==...==

# PI and P controller tests
add_sim_test(pi_p_tests SOURCES test_pi_p.cpp LINKS jit_solver json_io)

# ==...== Inspector Tests ==...==

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
)
target_link_libraries(inspector_tests PRIVATE
    blueprint_v2
    json_io
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(inspector_tests)

# ==...== Properties Window Tests ==...==

# Properties window tests (Phase 2: PropertiesWindow logic)
add_executable(properties_window_tests
    test_properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/editing_host.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/embedded_path_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
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
)
target_link_libraries(properties_window_tests PRIVATE
    blueprint_v2
    editor_semantic_canvas
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(properties_window_tests)

# ==...== Script Editor Window Tests ==...==

add_executable(script_editor_tests
    test_script_editor_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/script_editor_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/editing_host.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/embedded_path_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_compile_definitions(script_editor_tests PRIVATE EDITOR_TESTING)
target_include_directories(script_editor_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/ui
    ${CMAKE_SOURCE_DIR}/src/ui/core
    ${CMAKE_SOURCE_DIR}/src/ui/math
    ${CMAKE_SOURCE_DIR}/src/ui/layout
    ${CMAKE_SOURCE_DIR}/src/ui/renderer
)
target_link_libraries(script_editor_tests PRIVATE
    blueprint_v2
    editor_semantic_canvas
    jit_solver
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(script_editor_tests)

# ==...== Codegen Accumulator Tests ==...==

# Codegen accumulator regression tests — verify AOT emits accumulator-based domain scheduling
add_sim_test(codegen_accumulator_tests SOURCES test_codegen_accumulator.cpp LINKS an24_codegen jit_solver)

# ==...== AOT Composite Tests ==...==

# AOT Composite codegen tests (Phase 7: hierarchical AOT codegen)
add_sim_test(aot_composite_tests SOURCES test_aot_composite.cpp LINKS an24_codegen jit_solver blueprint_v2 LABELS "production_path;electrical")

# ==...== JIT-AOT Bridge Equivalence Tests ==...==

# Minimal bridge-based JIT vs AOT equivalence regression
add_sim_test(jit_aot_bridge_equivalence_tests SOURCES test_jit_aot_bridge_equivalence.cpp LINKS an24_codegen jit_solver LABELS "production_path;electrical")

# ==...== Production Path Parity Tests ==...==

add_sim_test(production_path_parity_tests SOURCES test_production_path_parity.cpp LINKS an24_codegen jit_solver blueprint_v2 LABELS "production_path;electrical")

add_sim_test(production_path_port_map_tests SOURCES test_production_path_port_map.cpp LINKS jit_solver LABELS "production_path")

add_sim_test(production_path_push_runtime_tests SOURCES test_production_path_push_runtime.cpp LINKS jit_solver json_io LABELS "production_path")

# ==...== New Component Tests ==...==

# AsymSlewRate component tests
add_sim_test(asym_slew_rate_tests SOURCES test_asym_slew_rate.cpp LINKS jit_solver json_io)

# Integrator component tests
add_sim_test(integrator_tests SOURCES test_integrator.cpp LINKS jit_solver json_io)

# Accumulator component tests
add_sim_test(accumulator_tests SOURCES test_accumulator.cpp LINKS jit_solver json_io)

# LuaScript component tests
add_sim_test(lua_script_tests SOURCES test_lua_script.cpp LINKS jit_solver json_io lua_static)

# Lua script validation tests
add_sim_test(lua_validate_tests SOURCES test_lua_script_validate.cpp LINKS jit_solver lua_static)

# 12SAM28 composite battery tests
add_sim_test(sam28_tests SOURCES test_12sam28.cpp LINKS jit_solver json_io)

# Clamp and Normalize component tests
add_sim_test(clamp_normalize_tests SOURCES test_clamp_normalize.cpp LINKS jit_solver json_io)

# Comparison component tests (Min/Max/Greater/Lesser/GreaterEq/LesserEq)
add_sim_test(comparison_components_tests SOURCES test_comparison_components.cpp LINKS jit_solver json_io)

# Spring component tests (mechanical domain)
add_sim_test(spring_tests SOURCES test_spring.cpp LINKS jit_solver json_io)

# ==...== V3 Migration Tests ==...==

add_executable(v3_migration_tests
    test_v3_migration.cpp
    test_closed_circuit_strict_load.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_include_directories(v3_migration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(v3_migration_tests PRIVATE
    blueprint_v2
    json_io
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(v3_migration_tests)

# ==...== Command Tests ==...==

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
)
target_link_libraries(command_tests PRIVATE
    blueprint_v2
    jit_solver
    json_io
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(command_tests)

# ==...== Unmapped Port Regression Tests ==...==

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
gtest_discover_tests(unmapped_port_regression_tests)

# ==...== Blueprint v2 Tests ==...==

add_sim_test(bp2_path_tests SOURCES blueprint_v2/test_path.cpp LINKS blueprint_v2)

add_sim_test(bp2_interface_tests SOURCES blueprint_v2/test_interface.cpp LINKS blueprint_v2 json_io)

# Blueprint v2: Blueprint class tests (Phase 3)
add_sim_test(bp2_blueprint_tests SOURCES blueprint_v2/test_blueprint.cpp test_issue_91_blueprint_instance_iface_authority.cpp LINKS blueprint_v2)

# Codec v2 tests (Phase 5)
add_sim_test(bp2_codec_tests SOURCES blueprint_v2/test_codec.cpp blueprint_v2/test_subwindow_open_target.cpp ${CMAKE_SOURCE_DIR}/src/editor/subwindow_open_target.cpp LINKS blueprint_v2 json_io)

# Flattener v2 tests (Phase 6)
add_sim_test(bp2_flattener_tests SOURCES blueprint_v2/test_flattener.cpp LINKS blueprint_v2)

# EditorModel v2 tests (Phase 7)
add_sim_test(bp2_editor_model_tests SOURCES blueprint_v2/test_editor_model.cpp blueprint_v2/test_embedded_editing_undo.cpp ${CMAKE_SOURCE_DIR}/src/editor/subwindow_open_target.cpp LINKS blueprint_v2)

add_sim_test(bp2_bake_tests SOURCES blueprint_v2/test_bake.cpp LINKS blueprint_v2 json_io)

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

# ==...== Electrical Primitives Tests ==...==

# Electrical primitives tests (Step 13: explicit primitive electrical nodes)
add_sim_test(electrical_primitives_tests SOURCES test_electrical_primitives.cpp LINKS jit_solver json_io LABELS "raw_builder;electrical")

# JIT↔AOT electrical parity fixtures (Phase 0-1: reference fixtures for semantic validation)
add_sim_test(electrical_parity_fixtures_tests SOURCES test_electrical_parity_fixtures.cpp LINKS jit_solver json_io an24_codegen LABELS "raw_builder;electrical")

# ==...== External Ref Signal Mapping Tests ==...==

# External reference signal mapping tests (parent-bound blueprint rendering)
add_executable(external_ref_signal_mapping_tests
    test_external_ref_signal_mapping.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/signal_key_resolver.cpp
)
target_include_directories(external_ref_signal_mapping_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(external_ref_signal_mapping_tests PRIVATE
    jit_solver
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(external_ref_signal_mapping_tests)

# ==...== Signal Key Resolver Tests ==...==

add_executable(signal_key_resolver_tests
    test_signal_key_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/signal_key_resolver.cpp
)
target_include_directories(signal_key_resolver_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(signal_key_resolver_tests PRIVATE
    jit_solver
    blueprint_v2
    GTest::gtest_main
)
gtest_discover_tests(signal_key_resolver_tests)

# ==...== Knob Switch Tests ==...==

# KnobSwitch (multi-position rotary selector) component tests
add_sim_test(knob_switch_tests SOURCES test_knob_switch.cpp LINKS jit_solver)

# ==...== Export Module Tests ==...==

# Export parity tests (Issue #38: unified elaboration via flattener)
add_sim_test(export_module_tests SOURCES blueprint_v2/test_export_flattener_parity.cpp LINKS blueprint_v2 json_io)

# Codegen elaboration parity tests (Issue #245: elaborate_for_codegen)
add_sim_test(codegen_export_parity_tests SOURCES blueprint_v2/test_codegen_export_parity.cpp LINKS blueprint_v2 json_io)

# ==...== Library Index Tests ==...==

# LibraryIndex tests (Issue #85: dedicated library index)
add_sim_test(bp2_library_index_tests SOURCES blueprint_v2/test_library_index.cpp blueprint_v2/test_type_def_to_blueprint.cpp LINKS blueprint_v2)

# ==...== Elaborate JIT Parity Tests ==...==

# elaborate_for_jit() vs legacy JSON path parity tests (Issue #113)
add_sim_test(elaborate_jit_parity_tests SOURCES test_elaborate_jit_parity.cpp LINKS blueprint_v2 jit_solver json_io LABELS "production_path")

# ==...== Cross-Path Signal Equivalence Tests ==...==

# Cross-path signal allocation equivalence tests (Issue #227)
add_sim_test(cross_path_signal_equivalence_tests SOURCES test_cross_path_signal_equivalence.cpp LINKS blueprint_v2 jit_solver json_io signal_allocation LABELS "production_path")
add_sim_test(pneumatic_integration_tests SOURCES test_pneumatic_integration.cpp LINKS jit_solver)

# ==...== SimVar Backend Trait Tests ==...==

# SimVar backend trait tests (Issue #425: compile-time backend selection)
add_sim_test(simvar_backend_tests SOURCES test_simvar_backend.cpp LINKS jit_solver)

# ==...== SimConnect Stub Client Tests ==...==

# SimConnect stub client tests (Issue #429: platform-agnostic interface + stub)
add_sim_test(simconnect_stub_tests SOURCES test_simconnect_stub.cpp LINKS simconnect_bridge)

# SimConnectProvider integration tests (Issue #432/#466: mapping + inject/extract + delta protocol)
add_sim_test(simconnect_bridge_tests SOURCES test_simconnect_bridge.cpp LINKS simconnect_bridge)

# SimConnectCoordinator tests (Issue #480: multi-document shared client)
add_sim_test(simconnect_coordinator_tests SOURCES test_simconnect_coordinator.cpp LINKS simconnect_bridge)

# MockProvider tests (Issue #460: SimVarProvider interface + mock)
add_sim_test(mock_provider_tests SOURCES test_mock_provider.cpp LINKS jit_solver)

# ProviderHost tests (Issue #461: ProviderHost + SimConnect adapter)
add_sim_test(provider_host_tests SOURCES test_provider_host.cpp LINKS simconnect_bridge jit_solver)

# Wire protocol binary packet tests (Issue #438/#439/#440: binary CommBus protocol)
add_sim_test(wire_protocol_tests SOURCES test_wire_protocol.cpp)

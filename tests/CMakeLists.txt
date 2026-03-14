# JIT Solver tests - Disabled (uses old Systems API, needs update to ComponentVariant)
# add_executable(jit_solver_tests
#     jit_solver_test.cpp
# )
# target_include_directories(jit_solver_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
# target_link_libraries(jit_solver_tests PRIVATE
#     jit_solver
#     GTest::gtest_main
# )
# include(GoogleTest)
# gtest_discover_tests(jit_solver_tests)

include(GoogleTest)

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
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(visual_wire_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(visual_wire_tests)

# Visual Hit Test (Phase 7: Scene-graph hit testing)
add_executable(visual_hittest_tests
    test_visual_hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
)
target_include_directories(visual_hittest_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(visual_hittest_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(visual_hittest_tests)

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

# Visual Content Widgets tests (Phase 3 - content widgets)
# BLOCKED: Circular dependency with nlohmann/json via data/node.h
# Tests are written but disabled until dependency issue is resolved.
# See SCENE_GRAPH_PROGRESS.md for details.
# add_executable(visual_content_widgets_tests
#     test_visual_content_widgets.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/visual/widgets/content_widgets.cpp
# )
# target_include_directories(visual_content_widgets_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_SOURCE_DIR}/src/editor
# )
# target_link_libraries(visual_content_widgets_tests PRIVATE
#     GTest::gtest_main
# )
# gtest_discover_tests(visual_content_widgets_tests)

# Visual NodeWidget tests (Phase 4-5 of Scene Graph Refactoring)
# Integrated test: includes json_parser for data/node.h dependency
add_executable(visual_node_widget_tests
    test_visual_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widgets/content_widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
)
target_include_directories(visual_node_widget_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(visual_node_widget_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(visual_node_widget_tests)

# Specialized node widget tests (Phase 9.2 of Scene Graph Refactoring)
# Tests for RefNodeWidget, TextNodeWidget, GroupNodeWidget, BusNodeWidget, NodeFactory
add_executable(specialized_node_widget_tests
    test_specialized_node_widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widgets/content_widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
)
target_include_directories(specialized_node_widget_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(specialized_node_widget_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(specialized_node_widget_tests)

# Scene mutations tests (Phase 10: Blueprint <-> visual::Scene sync)
add_executable(scene_mutations_tests
    test_scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widgets/content_widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_include_directories(scene_mutations_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(scene_mutations_tests PRIVATE
    json_parser
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(scene_mutations_tests)

# Port registry tests (TDD for codegen)
add_executable(port_registry_tests
    port_registry_test.cpp
)
target_include_directories(port_registry_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_compile_definitions(port_registry_tests PRIVATE
    TEST_DATA_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(port_registry_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(port_registry_tests)

# Port macro tests (TDD for PORTS macro)
add_executable(port_macro_tests
    port_macro_test.cpp
)
target_include_directories(port_macro_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_compile_definitions(port_macro_tests PRIVATE
    TEST_DATA_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(port_macro_tests PRIVATE
    json_parser
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
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_componentvariant_tests PRIVATE
    json_parser
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(editor_componentvariant_tests)

# Editor data tests (TDD Step 1)
add_executable(editor_data_tests
    test_data.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_include_directories(editor_data_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_data_tests PRIVATE
    json_parser
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(editor_data_tests)

# Editor data interning tests (InternedId migration)
add_executable(editor_data_interning_tests
    test_data_interning.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_include_directories(editor_data_interning_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_data_interning_tests PRIVATE
    json_parser
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(editor_data_interning_tests)


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
#     jit_solver
#     json_parser
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
target_include_directories(json_parser_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/json_parser)
target_link_libraries(json_parser_tests PRIVATE
    json_parser
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


# Editor simulation tests (TDD - integration with JIT solver)
add_executable(editor_simulation_tests
    test_simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_include_directories(editor_simulation_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_simulation_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(editor_simulation_tests)

# AZS (Circuit Breaker) component tests
add_executable(azs_tests
    test_azs.cpp
)
target_include_directories(azs_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(azs_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(azs_tests)

# Factory validation tests
add_executable(factory_validation_tests
    factory_validation_test.cpp
)
target_include_directories(factory_validation_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(factory_validation_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(factory_validation_tests)

# Logical solver tests (TDD - Comparator component)
add_executable(logical_solver_tests
    test_logical_solver.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_include_directories(logical_solver_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(logical_solver_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(logical_solver_tests)

# Blueprint ports tests (TDD - BlueprintInput/BlueprintOutput for hierarchical blueprints)
add_executable(blueprint_ports_tests
    test_blueprint_ports.cpp
)
target_include_directories(blueprint_ports_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/jit_solver
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(blueprint_ports_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(blueprint_ports_tests)

# Blueprint integration tests (TDD - BlueprintInput/BlueprintOutput pass-through)
add_executable(blueprint_integration_tests
    test_blueprint_integration.cpp
)
target_include_directories(blueprint_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/jit_solver
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(blueprint_integration_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(blueprint_integration_tests)

# Blueprint loading tests (Phase 2: fallback to blueprints/)
add_executable(blueprint_loading_tests
    test_blueprint_loading.cpp
)
target_include_directories(blueprint_loading_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/jit_solver
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(blueprint_loading_tests PRIVATE
    json_parser
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
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(provider_pattern_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(provider_pattern_tests)

# PID controller unit tests
add_executable(pid_tests
    test_pid.cpp
)
target_include_directories(pid_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(pid_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(pid_tests)

# PD controller tests
add_executable(pd_tests
    test_pd.cpp
)
target_include_directories(pd_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(pd_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(pd_tests)

# Subtract tests
add_executable(subtract_tests
    test_subtract.cpp
)
target_include_directories(subtract_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(subtract_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(subtract_tests)

# AsymTMO and FastTMO tests
add_executable(asym_tmo_tests
    test_asym_tmo.cpp
)
target_include_directories(asym_tmo_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(asym_tmo_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(asym_tmo_tests)

# SlewRate tests
add_executable(slew_rate_tests
    test_slew_rate.cpp
)
target_include_directories(slew_rate_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(slew_rate_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(slew_rate_tests)

# Logic gates tests
add_executable(logic_gates_tests
    test_logic_gates.cpp
)
target_include_directories(logic_gates_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(logic_gates_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(logic_gates_tests)

# LUT (Lookup Table) tests
add_executable(lut_tests
    test_lut.cpp
)
target_include_directories(lut_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(lut_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(lut_tests)

# LUT codegen tests (AOT code generation for LUT arena)
add_executable(lut_codegen_tests
    test_lut_codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/codegen/codegen.cpp
)
target_include_directories(lut_codegen_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(lut_codegen_tests PRIVATE
    json_parser
    jit_solver
    GTest::gtest_main
    spdlog::spdlog
)
gtest_discover_tests(lut_codegen_tests)

# Port mapping regression tests (catches missing ports in string_to_port_name)
add_executable(port_map_regression_tests
    test_port_map_regression.cpp
)
target_include_directories(port_map_regression_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(port_map_regression_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(port_map_regression_tests)

# AND gate debug integration test
add_executable(and_gate_debug_tests
    test_and_gate_debug.cpp
)
target_include_directories(and_gate_debug_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(and_gate_debug_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(and_gate_debug_tests)

# Codegen identifier sanitization regression tests
add_executable(codegen_sanitize_tests
    test_codegen_sanitize.cpp
)
target_include_directories(codegen_sanitize_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(codegen_sanitize_tests PRIVATE
    an24_codegen
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(codegen_sanitize_tests)

# V-to-Bool converter tests
add_executable(v_to_bool_tests
    test_v_to_bool.cpp
)
target_include_directories(v_to_bool_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(v_to_bool_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(v_to_bool_tests)

# Arithmetic operations tests
add_executable(arithmetic_tests
    test_arithmetic.cpp
)
target_include_directories(arithmetic_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(arithmetic_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(arithmetic_tests)

# PI and P controller tests
add_executable(pi_p_tests
    test_pi_p.cpp
)
target_include_directories(pi_p_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(pi_p_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(pi_p_tests)

# Inspector tests
add_executable(inspector_tests
    test_inspector.cpp
    ../src/editor/visual/inspector/inspector_core.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
)
target_compile_definitions(inspector_tests PRIVATE EDITOR_TESTING)
target_include_directories(inspector_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(inspector_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(inspector_tests)

# JIT integration test: Battery -> IndicatorLight voltage flow
add_executable(lamp_passthrough_jit_test
    test_lamp_passthrough_jit.cpp
)
target_include_directories(lamp_passthrough_jit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/jit_solver
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(lamp_passthrough_jit_test PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
    spdlog::spdlog
)
gtest_discover_tests(lamp_passthrough_jit_test)

# Editor hierarchical blueprint tests (TDD - collapsed node rendering, navigation, persistence)

# VisualPort tests (TDD - OOP port refactor)


# Refactoring regression tests (audit: extracted types, [[nodiscard]], dependency decoupling)

# Node deletion tests
add_executable(node_deletion_tests
    test_node_deletion.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    # New widget-based visual scene graph
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widgets/content_widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_compile_definitions(node_deletion_tests PRIVATE EDITOR_TESTING)
target_include_directories(node_deletion_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(node_deletion_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(node_deletion_tests)

# Multi-window tests
add_executable(multi_window_tests
    test_multi_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    # New widget-based visual scene graph
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widgets/content_widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_compile_definitions(multi_window_tests PRIVATE EDITOR_TESTING)
target_include_directories(multi_window_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(multi_window_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(multi_window_tests)

# Params integrity tests (Phase 0: data integrity for Properties Window)
add_executable(params_integrity_tests
    test_params_integrity.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/app.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_core.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    # New widget-based visual scene graph
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widgets/content_widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_compile_definitions(params_integrity_tests PRIVATE EDITOR_TESTING)
target_include_directories(params_integrity_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(params_integrity_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(params_integrity_tests)

# expand_type_definition tests (shared blueprint expansion)
add_executable(expand_typedef_tests
    test_expand_type_definition.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_compile_definitions(expand_typedef_tests PRIVATE EDITOR_TESTING)
target_include_directories(expand_typedef_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(expand_typedef_tests PRIVATE
    json_parser
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(expand_typedef_tests)


# Properties window tests (Phase 2: PropertiesWindow logic)
add_executable(properties_window_tests
    test_properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
)
target_compile_definitions(properties_window_tests PRIVATE EDITOR_TESTING)
target_include_directories(properties_window_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(properties_window_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(properties_window_tests)

# DT regression tests — FPS-independent physics, SOR constants, accumulator correctness
add_executable(dt_regression_tests
    test_dt_regression.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_include_directories(dt_regression_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(dt_regression_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(dt_regression_tests)

# Codegen accumulator regression tests — verify AOT emits accumulator-based domain scheduling
add_executable(codegen_accumulator_tests
    test_codegen_accumulator.cpp
    ${CMAKE_SOURCE_DIR}/src/codegen/codegen.cpp
)
target_include_directories(codegen_accumulator_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(codegen_accumulator_tests PRIVATE
    json_parser
    jit_solver
    GTest::gtest_main
    spdlog::spdlog
)
gtest_discover_tests(codegen_accumulator_tests)

# AOT Composite codegen tests (Phase 7: hierarchical AOT codegen)
add_executable(aot_composite_tests
    test_aot_composite.cpp
    ${CMAKE_SOURCE_DIR}/src/codegen/codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/json_parser/json_parser.cpp
)
target_include_directories(aot_composite_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(aot_composite_tests PRIVATE
    json_parser
    jit_solver
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(aot_composite_tests)

# Bake-in tests (Phase 4: bake-in conversion, params flatten)
add_executable(bake_in_tests
    test_bake_in.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_include_directories(bake_in_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(bake_in_tests PRIVATE
    json_parser
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(bake_in_tests)

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
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(asym_slew_rate_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(asym_slew_rate_tests)

# TimeDelay component tests
add_executable(time_delay_tests
    test_time_delay.cpp
)
target_include_directories(time_delay_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(time_delay_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(time_delay_tests)

# Monostable component tests
add_executable(monostable_tests
    test_monostable.cpp
)
target_include_directories(monostable_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(monostable_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(monostable_tests)

# SampleHold component tests
add_executable(sample_hold_tests
    test_sample_hold.cpp
)
target_include_directories(sample_hold_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(sample_hold_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(sample_hold_tests)

# Integrator component tests
add_executable(integrator_tests
    test_integrator.cpp
)
target_include_directories(integrator_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(integrator_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(integrator_tests)

# Clamp and Normalize component tests
add_executable(clamp_normalize_tests
    test_clamp_normalize.cpp
)
target_include_directories(clamp_normalize_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(clamp_normalize_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(clamp_normalize_tests)

# Comparison component tests (Min/Max/Greater/Lesser/GreaterEq/LesserEq)
add_executable(comparison_components_tests
    test_comparison_components.cpp
)
target_include_directories(comparison_components_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(comparison_components_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(comparison_components_tests)

# Spring component tests (mechanical domain)
add_executable(spring_tests
    test_spring.cpp
)
target_include_directories(spring_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(spring_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(spring_tests)

# Sub-blueprint reference tests (TDD - SubBlueprintInstance data structures)
add_executable(sub_blueprint_ref_tests
    test_sub_blueprint_ref.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/codegen/codegen.cpp
    ${CMAKE_SOURCE_DIR}/src/json_parser/json_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_include_directories(sub_blueprint_ref_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(sub_blueprint_ref_tests PRIVATE
    json_parser
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(sub_blueprint_ref_tests)

# Document + WindowSystem tests (MDI lifecycle, regression tests)
add_executable(document_window_system_tests
    test_document_window_system.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window_system.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_core.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    # New widget-based visual scene graph
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widgets/content_widgets.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
)
target_compile_definitions(document_window_system_tests PRIVATE EDITOR_TESTING)
target_include_directories(document_window_system_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(document_window_system_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(document_window_system_tests)

# Recent files tests
add_executable(recent_files_tests
    test_recent_files.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/recent_files.cpp
)
target_include_directories(recent_files_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(recent_files_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(recent_files_tests)

# FlatBlueprint format tests (was v2 format)
add_executable(flat_blueprint_tests
    test_blueprint_v2.cpp
)
target_include_directories(flat_blueprint_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(flat_blueprint_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(flat_blueprint_tests)

# Library conversion tests
add_executable(library_v2_tests
    test_library_v2.cpp
)
target_include_directories(library_v2_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(library_v2_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(library_v2_tests)


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

# Editor persist tests (TDD Step 2)
add_executable(editor_persist_tests
    test_persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_compile_definitions(editor_persist_tests PRIVATE EDITOR_TESTING)
target_include_directories(editor_persist_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_persist_tests PRIVATE
    json_parser
    jit_solver
    GTest::gtest_main
)
gtest_discover_tests(editor_persist_tests)

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
add_executable(editor_render_tests
    test_render.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
)
target_compile_definitions(editor_render_tests PRIVATE EDITOR_TESTING)
target_include_directories(editor_render_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_render_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(editor_render_tests)

# Editor hit test tests (TDD Step 6)
add_executable(editor_hittest_tests
    test_hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
)
target_compile_definitions(editor_hittest_tests PRIVATE EDITOR_TESTING)
target_include_directories(editor_hittest_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_hittest_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(editor_hittest_tests)

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
add_executable(editor_router_tests
    test_router.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_include_directories(editor_router_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_router_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(editor_router_tests)

# Widget & Layout tests
add_executable(editor_widget_tests
    test_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_compile_definitions(editor_widget_tests PRIVATE EDITOR_TESTING)
target_include_directories(editor_widget_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_widget_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(editor_widget_tests)

# Editor simulation tests (TDD - integration with JIT solver)
add_executable(editor_simulation_tests
    test_simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
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

# Factory validation tests (TDD - Task 2) - Disabled (uses old create_component API)
# add_executable(factory_validation_tests
#     factory_validation_test.cpp
# )
# target_include_directories(factory_validation_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_SOURCE_DIR}/src/json_parser
#     ${CMAKE_BINARY_DIR}/_deps/json-src/include
# )
# target_link_libraries(factory_validation_tests PRIVATE
#     jit_solver
#     json_parser
#     GTest::gtest_main
# )
# gtest_discover_tests(factory_validation_tests)

# Logical solver tests (TDD - Comparator component)
add_executable(logical_solver_tests
    test_logical_solver.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
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
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
)
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
add_executable(editor_hierarchical_tests
    test_editor_hierarchical.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
)
target_compile_definitions(editor_hierarchical_tests PRIVATE EDITOR_TESTING)
target_include_directories(editor_hierarchical_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(editor_hierarchical_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(editor_hierarchical_tests)

# VisualPort tests (TDD - OOP port refactor)
add_executable(visual_port_tests
    test_visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
)
target_compile_definitions(visual_port_tests PRIVATE EDITOR_TESTING)
target_include_directories(visual_port_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(visual_port_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(visual_port_tests)

# VisualScene tests
add_executable(visual_scene_tests
    test_visual_scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
)
target_compile_definitions(visual_scene_tests PRIVATE EDITOR_TESTING)
target_include_directories(visual_scene_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(visual_scene_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(visual_scene_tests)

# Node deletion tests
add_executable(node_deletion_tests
    test_node_deletion.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
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
    ${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
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
    ${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
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

# Context menu tests (Phase 1: right-click on node)
add_executable(context_menu_tests
    test_context_menu.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
)
target_compile_definitions(context_menu_tests PRIVATE EDITOR_TESTING)
target_include_directories(context_menu_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(context_menu_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(context_menu_tests)

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
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
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

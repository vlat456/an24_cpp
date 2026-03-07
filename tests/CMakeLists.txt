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
    GTest::gtest_main
)
gtest_discover_tests(editor_data_tests)

# Editor persist tests (TDD Step 2)
add_executable(editor_persist_tests
    test_persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/render.cpp
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
    ${CMAKE_SOURCE_DIR}/src/editor/render.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
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
# add_executable(editor_hittest_tests
#     test_hittest.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/hittest.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
#     ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
# )
# target_include_directories(editor_hittest_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/src
#     ${CMAKE_SOURCE_DIR}/src/editor
# )
# target_link_libraries(editor_hittest_tests PRIVATE
#     GTest::gtest_main
# )
# gtest_discover_tests(editor_hittest_tests)

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
    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/render.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
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
    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/render.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
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
    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
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
    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
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

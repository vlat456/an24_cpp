include(GoogleTest)

# Editor data tests (TDD Step 1)
add_executable(editor_data_tests
    test_data.cpp
)
target_include_directories(editor_data_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(editor_data_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_data_tests)

# Editor persist tests (TDD Step 2)
add_executable(editor_persist_tests
    test_persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
)
target_include_directories(editor_persist_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(editor_persist_tests PRIVATE
    json_parser
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

# Editor interaction tests (TDD Step 4)
add_executable(editor_interaction_tests
    test_interaction.cpp
)
target_include_directories(editor_interaction_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(editor_interaction_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_interaction_tests)

# Editor render tests (TDD Step 5) - disabled, needs app.cpp
## add_executable(editor_render_tests
#    test_render.cpp
#    ${CMAKE_SOURCE_DIR}/src/editor/render.cpp
#    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
#    ${CMake_SOURCE_DIR}/src/editor/widget.cpp
#    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
#    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
#)
#target_compile_definitions(editor_render_tests PRIVATE EDITOR_TESTING)
#target_include_directories(editor_render_tests PRIVATE
#    ${CMAKE_SOURCE_DIR}/src
#    ${CMAKE_SOURCE_DIR}/src/editor
#    ${json_SOURCE_DIR}/include
#)
#target_link_libraries(editor_render_tests PRIVATE
#    GTest::gtest_main
#    jit_solver
#    json_parser
#)
#gtest_discover_tests(editor_render_tests)

# Editor hit test tests (TDD Step 6)
add_executable(editor_hittest_tests
    test_hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
)
target_include_directories(editor_hittest_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(editor_hittest_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_hittest_tests)

# Editor event tests (TDD Step 7)
add_executable(editor_event_tests
    test_events.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/persist.cpp
)
target_include_directories(editor_event_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/build/_deps/json-src/include
)
target_link_libraries(editor_event_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(editor_event_tests)

# GL setup configuration tests (TDD Step 9)
add_executable(editor_gl_setup_tests
    test_gl_setup.cpp
)
target_include_directories(editor_gl_setup_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(editor_gl_setup_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_gl_setup_tests)

add_executable(json_parser_tests
    json_parser_test.cpp
)
target_include_directories(json_parser_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/json_parser)
target_link_libraries(json_parser_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(json_parser_tests)

# Push-based solver tests
add_executable(push_solver_tests
    test_push_solver.cpp
)
target_include_directories(push_solver_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(push_solver_tests PRIVATE
    jit_solver
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(push_solver_tests)

# Editor routing tests (TDD - routing points)
add_executable(editor_routing_tests
    test_routing.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/wires/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
)
target_include_directories(editor_routing_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(editor_routing_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_routing_tests)

# A* Router tests
add_executable(editor_router_tests
    test_router.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
)
target_include_directories(editor_router_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(editor_router_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_router_tests)

# Widget & Layout tests
add_executable(editor_widget_tests
    test_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
)
target_compile_definitions(editor_widget_tests PRIVATE EDITOR_TESTING)
target_include_directories(editor_widget_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
)
target_link_libraries(editor_widget_tests PRIVATE
    GTest::gtest_main
)
gtest_discover_tests(editor_widget_tests)

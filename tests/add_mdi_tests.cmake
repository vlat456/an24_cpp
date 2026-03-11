# MDI Document and WindowSystem tests
add_executable(mdi_tests
    document_tests.cpp
    # Editor source files needed
    ${CMAKE_SOURCE_DIR}/src/editor/document.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window_system.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/layout.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/tooltip_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_core.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_render.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/imgui_theme.cpp
)
target_include_directories(mdi_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(mdi_tests PRIVATE
    jit_solver
    json_parser
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(mdi_tests)

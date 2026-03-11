# MDI Document and WindowSystem tests
# Add all necessary editor source files (excluding SDL/OpenGL/ImGui dependencies)
add_executable(mdi_tests
    document_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window_system.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/pt.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/canvas_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/input/input_types.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/layout.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/spatial_grid.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/wire_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/viewport/viewport.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/blueprint_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_core.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/display_tree.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/blueprint_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/node_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/wire_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
)
target_include_directories(mdi_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json_parser
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(mdi_tests PRIVATE
    IMGUI_DISABLE_INCLUDE_IMGUI_H
)
target_link_libraries(mdi_tests PRIVATE
    jit_solver
    json_parser
    spdlog::spdlog
    GTest::gtest_main
)
gtest_discover_tests(mdi_tests)

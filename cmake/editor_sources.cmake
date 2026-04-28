# Editor source lists — single source of truth for examples/ and tests/
# Include this file from both examples/CMakeLists.txt and tests/CMakeLists.txt

# Visual core — scene graph, widgets, wires, ports, primitives
set(EDITOR_VISUAL_SOURCES
    ${CMAKE_SOURCE_DIR}/src/editor/visual/widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene_mutations.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/wire/wire.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/port/visual_port.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/primitives/primitives.cpp
)

# Visual nodes — blueprint node widgets
set(EDITOR_VISUAL_NODE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/visual_node.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/ref_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/text_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/group_node_widget.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/node/bus_node_widget.cpp
)

# Full visual sources = core + nodes
set(EDITOR_VISUAL_FULL_SOURCES
    ${EDITOR_VISUAL_SOURCES}
    ${EDITOR_VISUAL_NODE_SOURCES}
)

# Input/canvas — mouse handling, editing, viewport
set(EDITOR_CANVAS_INPUT_SOURCES
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
)

# Document model — core editor logic, history, IO, simulation
set(EDITOR_DOCUMENT_SOURCES
    ${CMAKE_SOURCE_DIR}/src/editor/document.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_components.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/scope_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_history.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_io.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_simulation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/document_windows.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window_system.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/oscilloscope.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/window/properties_window.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/subwindow_open_target.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/signal_key_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/workspace_session_persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_analysis.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_build.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/commands/extract_blueprint_common.cpp
)

# ImGui rendering shell — only used by the visual editor, not by tests
set(EDITOR_IMGUI_SHELL_SOURCES
    ${CMAKE_SOURCE_DIR}/src/editor/icon_font.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/imgui_theme.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/canvas_renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/oscilloscope_plot.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/renderer/grid_renderer.cpp
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
    ${CMAKE_SOURCE_DIR}/src/editor/visual/inspector/inspector_render.cpp
)

# ImGui third-party sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# All testable editor sources combined (used by an24_editor + tests)
set(EDITOR_ALL_TESTABLE_SOURCES
    ${EDITOR_DOCUMENT_SOURCES}
    ${EDITOR_CANVAS_INPUT_SOURCES}
    ${EDITOR_VISUAL_FULL_SOURCES}
)

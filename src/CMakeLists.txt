add_subdirectory(core/solvers/jit)
add_subdirectory(core/registry)
add_subdirectory(io/json)
add_subdirectory(blueprint_v2)

add_library(editor_semantic_canvas STATIC
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/semantic_input_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/semantic_input_reducer.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/semantic_interaction.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/semantic_interaction_session.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/semantic_scene_hittest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/semantic_scene_snapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/semantic_scene_input.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/node_slot_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/node_presentation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/presentation/canvas_scene_snapshot.cpp
)
target_include_directories(editor_semantic_canvas PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(editor_semantic_canvas PUBLIC blueprint_v2 spdlog::spdlog)

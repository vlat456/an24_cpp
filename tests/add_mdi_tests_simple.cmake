# MDI Document and WindowSystem tests
# Note: These tests have minimal ImGui stubs and don't link full editor sources
add_executable(mdi_tests
    document_tests.cpp
    # Only include editor sources that don't depend on ImGui
    ${CMAKE_SOURCE_DIR}/src/editor/document.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/visual/scene/persist.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/simulation.cpp
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

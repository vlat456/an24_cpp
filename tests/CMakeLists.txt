add_executable(jit_solver_tests
    jit_solver_test.cpp
)
target_include_directories(jit_solver_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jit_solver_tests PRIVATE
    jit_solver
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(jit_solver_tests)

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

add_executable(json_parser_tests
    json_parser_test.cpp
)
target_include_directories(json_parser_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/json_parser)
target_link_libraries(json_parser_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(json_parser_tests)

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

add_executable(json_parser_tests
    json_parser_test.cpp
)
target_include_directories(json_parser_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/json_parser)
target_link_libraries(json_parser_tests PRIVATE
    json_parser
    GTest::gtest_main
)
gtest_discover_tests(json_parser_tests)

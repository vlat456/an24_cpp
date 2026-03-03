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

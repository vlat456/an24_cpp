add_executable(hello_world main.cpp)
target_include_directories(hello_world PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(hello_world PRIVATE jit_solver spdlog::spdlog)

add_executable(components_test components_test.cpp)
target_include_directories(components_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(components_test PRIVATE jit_solver spdlog::spdlog)

add_executable(debug_test debug.cpp)
target_include_directories(debug_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(debug_test PRIVATE jit_solver spdlog::spdlog)

add_executable(json_test json_test.cpp)
target_include_directories(json_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(json_test PRIVATE json_parser jit_solver spdlog::spdlog)

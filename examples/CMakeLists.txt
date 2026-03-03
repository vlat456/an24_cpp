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

add_library(an24_codegen STATIC
    ${CMAKE_SOURCE_DIR}/src/codegen/codegen.cpp
)
target_include_directories(an24_codegen PUBLIC ${CMAKE_SOURCE_DIR}/src)

add_executable(codegen_test codegen_test.cpp)
target_include_directories(codegen_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(codegen_test PRIVATE an24_codegen json_parser jit_solver spdlog::spdlog)

# Compile and run generated code
add_executable(test_generated
    ${CMAKE_SOURCE_DIR}/generated/test_generated.cpp
    ${CMAKE_SOURCE_DIR}/generated/generated_an24_composite_test.cpp
)
target_include_directories(test_generated PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
target_link_libraries(test_generated PRIVATE jit_solver spdlog::spdlog)

add_executable(aot_test
    ${CMAKE_SOURCE_DIR}/generated/aot_test.cpp
    ${CMAKE_SOURCE_DIR}/generated/generated_an24_composite_test.cpp
)
target_include_directories(aot_test PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/generated)
target_link_libraries(aot_test PRIVATE jit_solver spdlog::spdlog)

# Standalone tool: regenerate port_registry.h from library/*.blueprint
add_executable(update_port_registry update_port_registry.cpp)
target_include_directories(update_port_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(update_port_registry PRIVATE an24_codegen json_parser spdlog::spdlog)

# Convenience target: run the tool from the project root so relative paths work
# Usage: cmake --build build --target regenerate_port_registry
add_custom_target(regenerate_port_registry
    COMMAND update_port_registry
                "${CMAKE_SOURCE_DIR}/library"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS update_port_registry
    COMMENT "Regenerating port_registry.h from library/*.blueprint"
    VERBATIM
)

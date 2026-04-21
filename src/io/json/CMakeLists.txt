add_library(json_io
    component_registry_json_loader.cpp
    json_internal_utils.cpp
    parse_json_api.cpp
    type_definition_json.cpp
)

target_include_directories(json_io PUBLIC
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(json_io PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

target_link_libraries(json_io PUBLIC
    core_registry
)

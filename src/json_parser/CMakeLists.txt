add_library(json_parser
    json_parser.cpp
    json_parser_internal_utils.cpp
    json_parser_types.cpp
    json_parser_model.cpp
    json_parser_serialize.cpp
    json_parser_registry.cpp
)
target_include_directories(json_parser PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(json_parser PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

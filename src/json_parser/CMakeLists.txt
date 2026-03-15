add_library(json_parser
    json_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/flat_blueprint.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/data/type_def_convert.cpp
)
target_include_directories(json_parser PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(json_parser PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

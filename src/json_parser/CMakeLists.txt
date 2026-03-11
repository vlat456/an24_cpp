add_library(json_parser
    json_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/v2/convert.cpp
)
target_include_directories(json_parser PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
    ${CMAKE_SOURCE_DIR}/src/v2
)
target_link_libraries(json_parser PUBLIC blueprint_v2 PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

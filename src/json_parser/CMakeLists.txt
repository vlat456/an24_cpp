add_library(json_parser
    json_parser.cpp
)
target_include_directories(json_parser PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(json_parser PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

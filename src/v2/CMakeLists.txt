add_library(blueprint_v2
    blueprint_v2.cpp
)
target_include_directories(blueprint_v2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/json-src/include
)
target_link_libraries(blueprint_v2 PRIVATE nlohmann_json::nlohmann_json spdlog::spdlog)

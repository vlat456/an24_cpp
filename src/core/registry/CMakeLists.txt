add_library(core_registry STATIC
    ${CMAKE_SOURCE_DIR}/src/core/model/component_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/core/model/catalog_registry.cpp
    component_resolution.cpp
)

target_include_directories(core_registry PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(core_registry PRIVATE
    spdlog::spdlog
)

add_library(blueprint_v2 STATIC
    path/path.cpp
    interface/interface.cpp
    blueprint/blueprint.cpp
    registry/type_registry.cpp
    codec/blueprint_codec.cpp
    flattener/flattener.cpp
    editor_model/editor_model.cpp
    validation/path_resolver.cpp
    validation/wire_validator.cpp
    validation/invariant_checker.cpp
    diagnostics/repair.cpp
    bake/bake_ops.cpp
)
target_include_directories(blueprint_v2 PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(blueprint_v2 PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    json_parser
)

add_library(blueprint_v2 STATIC
    path/path.cpp
    interface/interface.cpp
    interface/node_port_projection.cpp
    blueprint/blueprint.cpp
    blueprint/canonicalize.cpp
    library/blueprint_library.cpp
    library/library_path.cpp
    codec/blueprint_codec.cpp
    codec/blueprint_codec_common.cpp
    codec/blueprint_codec_encode.cpp
    codec/blueprint_codec_decode.cpp
    flattener/flattener.cpp
    elaboration/sim_export.cpp
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

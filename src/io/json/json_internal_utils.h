#pragma once

#include "core/model/component_spec.h"

#include <nlohmann/json.hpp>

namespace json_io_internal {

struct ParamSchemaEntry {
    ParamSchemaType type = ParamSchemaType::String;
    std::optional<double> min;
    std::optional<double> max;
    bool required = false;
    bool visual_only = false;
    std::string arena_field_offset;
    std::string arena_field_size;
};

Domain parse_domain_string(const std::string& s);
Domain parse_domain_mask_int(int v);
ParamSchemaType parse_param_schema_type(const std::string& s);
std::unordered_map<std::string, ParamSchemaEntry> parse_param_schema(const nlohmann::json& j);
PortType parse_port_type_string(const std::string& s);

void merge_params_and_schema(
    const nlohmann::json& j,
    const std::string& defaults_key,
    std::unordered_map<std::string, ParamSpec>& out_params);

} // namespace json_io_internal

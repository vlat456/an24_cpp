#pragma once

#include "json_parser.h"

#include <nlohmann/json.hpp>

namespace json_parser_internal {

/// Parser-internal schema entry — not exposed to consumers.
struct ParamSchemaEntry {
    ParamSchemaType type = ParamSchemaType::String;
    std::optional<double> min;
    std::optional<double> max;
    bool required = false;
    bool visual_only = false;
};

Domain parse_domain_string(const std::string& s);
Domain parse_domain_mask_int(int v);
ParamSchemaType parse_param_schema_type(const std::string& s);
std::unordered_map<std::string, ParamSchemaEntry> parse_param_schema(const nlohmann::json& j);
PortType parse_port_type_string(const std::string& s);
std::string port_type_to_string(PortType t);

/// Merge parsed param_defaults + param_schema into unified ParamSpec map on a TypeDefinition.
void merge_params_and_schema(
    const nlohmann::json& j,
    const std::string& defaults_key,
    std::unordered_map<std::string, ParamSpec>& out_params);

void validate_params_against_schema(
    const std::unordered_map<std::string, std::string>& params,
    const std::unordered_map<std::string, ParamSpec>& schema,
    const std::string& dev_name,
    const std::string& classname);

} // namespace json_parser_internal

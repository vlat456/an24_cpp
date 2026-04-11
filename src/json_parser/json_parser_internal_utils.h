#pragma once

#include "json_parser.h"

#include <nlohmann/json.hpp>

namespace json_parser_internal {

Domain parse_domain_string(const std::string& s);
Domain parse_domain_mask_int(int v);
ParamSchemaType parse_param_schema_type(const std::string& s);
std::unordered_map<std::string, ParamSchemaEntry> parse_param_schema(const nlohmann::json& j);
PortType parse_port_type_string(const std::string& s);

void validate_params_against_schema(
    const std::unordered_map<std::string, std::string>& params,
    const std::unordered_map<std::string, ParamSchemaEntry>& schema,
    const std::string& dev_name,
    const std::string& classname);

} // namespace json_parser_internal

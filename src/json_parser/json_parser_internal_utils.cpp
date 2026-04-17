#include "json_parser_internal_utils.h"

#include "../parse_number.h"

namespace json_parser_internal {

Domain parse_domain_string(const std::string& s) {
    if (s == "Electrical") return Domain::Electrical;
    if (s == "Logical") return Domain::Logical;
    if (s == "Hydraulic") return Domain::Hydraulic;
    if (s == "Mechanical") return Domain::Mechanical;
    if (s == "Thermal") return Domain::Thermal;
    throw std::runtime_error("Unknown domain: " + s);
}

Domain parse_domain_mask_int(int v) {
    if (v <= 0 || (v & ~31) != 0) {
        throw std::runtime_error("Invalid domain bitmask value: " + std::to_string(v));
    }
    return static_cast<Domain>(static_cast<uint8_t>(v));
}

ParamSchemaType parse_param_schema_type(const std::string& s) {
    if (s == "float") return ParamSchemaType::Float;
    if (s == "int") return ParamSchemaType::Int;
    if (s == "bool") return ParamSchemaType::Bool;
    if (s == "string") return ParamSchemaType::String;
    throw std::runtime_error("Unknown param schema type: " + s);
}

std::unordered_map<std::string, ParamSchemaEntry> parse_param_schema(const nlohmann::json& j) {
    std::unordered_map<std::string, ParamSchemaEntry> out;
    if (!j.is_object()) {
        throw std::runtime_error("'param_schema' must be an object");
    }
    for (const auto& [name, entry] : j.items()) {
        if (!entry.is_object()) {
            throw std::runtime_error("param_schema entry '" + name + "' must be object");
        }
        if (!entry.contains("type") || !entry["type"].is_string()) {
            throw std::runtime_error("param_schema entry '" + name + "' missing string 'type'");
        }
        ParamSchemaEntry e;
        e.type = parse_param_schema_type(entry["type"].get<std::string>());
        if (entry.contains("required")) {
            if (!entry["required"].is_boolean()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'required' must be bool");
            }
            e.required = entry["required"].get<bool>();
        }
        if (entry.contains("min")) {
            if (!entry["min"].is_number()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'min' must be number");
            }
            e.min = entry["min"].get<double>();
        }
        if (entry.contains("max")) {
            if (!entry["max"].is_number()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'max' must be number");
            }
            e.max = entry["max"].get<double>();
        }
        if (e.min.has_value() && e.max.has_value() && *e.min > *e.max) {
            throw std::runtime_error("param_schema entry '" + name + "' has min > max");
        }
        if (entry.contains("visual_only")) {
            if (!entry["visual_only"].is_boolean()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'visual_only' must be bool");
            }
            e.visual_only = entry["visual_only"].get<bool>();
        }
        out[name] = e;
    }
    return out;
}

PortType parse_port_type_string(const std::string& s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Signal") return PortType::Any;
    if (s == "Fraction") return PortType::Any;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    if (s == "Contextual") return PortType::Contextual;
    if (s == "Any") return PortType::Any;
    throw std::runtime_error("Unknown port type: " + s);
}

void validate_params_against_schema(
    const std::unordered_map<std::string, std::string>& params,
    const std::unordered_map<std::string, ParamSchemaEntry>& schema,
    const std::string& dev_name,
    const std::string& classname)
{
    for (const auto& [name, entry] : schema) {
        auto it = params.find(name);
        if (it == params.end()) {
            if (entry.required) {
                throw std::runtime_error("Missing required parameter '" + name + "' on device '" + dev_name + "' (" + classname + ")");
            }
            continue;
        }
        const std::string& value = it->second;
        switch (entry.type) {
            case ParamSchemaType::Float: {
                float v = 0.0f;
                if (!locale_safe::parse_float(value, v)) {
                    throw std::runtime_error("Parameter '" + name + "' must be float on device '" + dev_name + "' (" + classname + ")");
                }
                if (entry.min.has_value() && static_cast<double>(v) < *entry.min) {
                    throw std::runtime_error("Parameter '" + name + "' below min on device '" + dev_name + "' (" + classname + ")");
                }
                if (entry.max.has_value() && static_cast<double>(v) > *entry.max) {
                    throw std::runtime_error("Parameter '" + name + "' above max on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            }
            case ParamSchemaType::Int: {
                long long v = 0;
                if (!locale_safe::parse_int64(value, v)) {
                    throw std::runtime_error("Parameter '" + name + "' must be int on device '" + dev_name + "' (" + classname + ")");
                }
                if (entry.min.has_value() && static_cast<double>(v) < *entry.min) {
                    throw std::runtime_error("Parameter '" + name + "' below min on device '" + dev_name + "' (" + classname + ")");
                }
                if (entry.max.has_value() && static_cast<double>(v) > *entry.max) {
                    throw std::runtime_error("Parameter '" + name + "' above max on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            }
            case ParamSchemaType::Bool: {
                if (!(value == "true" || value == "false" || value == "1" || value == "0")) {
                    throw std::runtime_error("Parameter '" + name + "' must be bool on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            }
            case ParamSchemaType::String:
                break;
        }
    }
}

} // namespace json_parser_internal

#include "io/json/json_internal_utils.h"

#include "parse_number.h"

namespace json_io_internal {

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
    if (s == "table") return ParamSchemaType::Table;
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
        ParamSchemaEntry schema;
        schema.type = parse_param_schema_type(entry["type"].get<std::string>());
        if (entry.contains("required")) {
            if (!entry["required"].is_boolean()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'required' must be bool");
            }
            schema.required = entry["required"].get<bool>();
        }
        if (entry.contains("min")) {
            if (!entry["min"].is_number()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'min' must be number");
            }
            schema.min = entry["min"].get<double>();
        }
        if (entry.contains("max")) {
            if (!entry["max"].is_number()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'max' must be number");
            }
            schema.max = entry["max"].get<double>();
        }
        if (schema.min.has_value() && schema.max.has_value() && *schema.min > *schema.max) {
            throw std::runtime_error("param_schema entry '" + name + "' has min > max");
        }
        if (entry.contains("visual_only")) {
            if (!entry["visual_only"].is_boolean()) {
                throw std::runtime_error("param_schema entry '" + name + "' field 'visual_only' must be bool");
            }
            schema.visual_only = entry["visual_only"].get<bool>();
        }
        if (entry.contains("arena_field_offset")) {
            schema.arena_field_offset = entry["arena_field_offset"].get<std::string>();
        }
        if (entry.contains("arena_field_size")) {
            schema.arena_field_size = entry["arena_field_size"].get<std::string>();
        }
        if (entry.contains("field")) {
            schema.field = entry["field"].get<std::string>();
        }
        out[name] = schema;
    }
    return out;
}

PortType parse_port_type_string(const std::string& s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Signal") return PortType::Signal;
    if (s == "Fraction") return PortType::Signal;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    if (s == "Contextual") return PortType::Contextual;
    if (s == "Any") return PortType::Any;
    throw std::runtime_error("Unknown port type: " + s);
}

static ParamSpec to_param_spec(const ParamSchemaEntry& schema, std::string default_value = {}) {
    ParamSpec spec;
    spec.type = schema.type;
    spec.default_value = std::move(default_value);
    spec.min = schema.min;
    spec.max = schema.max;
    spec.required = schema.required;
    spec.visual_only = schema.visual_only;
    spec.arena_field_offset = schema.arena_field_offset;
    spec.arena_field_size = schema.arena_field_size;
    spec.field = schema.field;
    return spec;
}

void merge_params_and_schema(
    const nlohmann::json& j,
    const std::string& defaults_key,
    std::unordered_map<std::string, ParamSpec>& out_params)
{
    std::unordered_map<std::string, ParamSchemaEntry> schema;
    if (j.contains("param_schema")) {
        schema = parse_param_schema(j["param_schema"]);
    }

    auto apply_schema = [&](const std::string& key, ParamSpec& spec) {
        auto it = schema.find(key);
        if (it != schema.end()) {
            spec.type = it->second.type;
            spec.min = it->second.min;
            spec.max = it->second.max;
            spec.required = it->second.required;
            spec.visual_only = it->second.visual_only;
            spec.arena_field_offset = it->second.arena_field_offset;
            spec.arena_field_size = it->second.arena_field_size;
            spec.field = it->second.field;
        }
    };

    if (j.contains(defaults_key) && j[defaults_key].is_object()) {
        for (auto& [key, value] : j[defaults_key].items()) {
            ParamSpec spec;
            if (value.is_string()) {
                spec.default_value = value.get<std::string>();
            } else if (value.is_number()) {
                spec.default_value = locale_safe::format_float(static_cast<float>(value.get<double>()));
            } else if (value.is_object() && value.contains("default")) {
                spec.default_value = value["default"].get<std::string>();
            }
            apply_schema(key, spec);
            out_params[key] = std::move(spec);
        }

        for (auto& [key, entry] : schema) {
            if (out_params.find(key) == out_params.end()) {
                out_params[key] = to_param_spec(entry);
            }
        }
    } else if (!schema.empty()) {
        for (auto& [key, entry] : schema) {
            out_params[key] = to_param_spec(entry);
        }
    }
}

} // namespace json_io_internal

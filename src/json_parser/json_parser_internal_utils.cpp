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

std::string port_type_to_string(PortType t) {
    switch (t) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Signal: return "Signal";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Contextual: return "Contextual";
        case PortType::Any: return "Any";
    }
    return "Unknown";
}

static ParamSpec to_param_spec(const ParamSchemaEntry& e, std::string default_value = {}) {
    ParamSpec spec;
    spec.type = e.type;
    spec.default_value = std::move(default_value);
    spec.min = e.min;
    spec.max = e.max;
    spec.required = e.required;
    spec.visual_only = e.visual_only;
    return spec;
}

void merge_params_and_schema(
    const nlohmann::json& j,
    const std::string& defaults_key,
    std::unordered_map<std::string, ParamSpec>& out_params)
{
    std::unordered_map<std::string, ParamSchemaEntry> temp_schema;
    if (j.contains("param_schema")) {
        temp_schema = parse_param_schema(j["param_schema"]);
    }

    auto apply_schema = [&](const std::string& key, ParamSpec& spec) {
        auto it = temp_schema.find(key);
        if (it != temp_schema.end()) {
            spec.type = it->second.type;
            spec.min = it->second.min;
            spec.max = it->second.max;
            spec.required = it->second.required;
            spec.visual_only = it->second.visual_only;
        }
    };

    if (j.contains(defaults_key) && j[defaults_key].is_object()) {
        for (auto& [k, v] : j[defaults_key].items()) {
            ParamSpec spec;
            if (v.is_string()) {
                spec.default_value = v.get<std::string>();
            } else if (v.is_number()) {
                spec.default_value = locale_safe::format_float(static_cast<float>(v.get<double>()));
            } else if (v.is_object() && v.contains("default")) {
                spec.default_value = v["default"].get<std::string>();
            }
            apply_schema(k, spec);
            out_params[k] = std::move(spec);
        }

        // Schema-only params (no default in JSON)
        for (auto& [k, schema] : temp_schema) {
            if (out_params.find(k) == out_params.end()) {
                out_params[k] = to_param_spec(schema);
            }
        }
    } else if (!temp_schema.empty()) {
        // param_schema without defaults
        for (auto& [k, schema] : temp_schema) {
            out_params[k] = to_param_spec(schema);
        }
    }
}

void validate_params_against_schema(
    const std::unordered_map<std::string, std::string>& params,
    const std::unordered_map<std::string, ParamSpec>& schema,
    const std::string& dev_name,
    const std::string& classname)
{
    for (const auto& [name, spec] : schema) {
        auto it = params.find(name);
        if (it == params.end()) {
            if (spec.required) {
                throw std::runtime_error("Missing required parameter '" + name + "' on device '" + dev_name + "' (" + classname + ")");
            }
            continue;
        }
        const std::string& value = it->second;
        switch (spec.type) {
            case ParamSchemaType::Float: {
                float v = 0.0f;
                if (!locale_safe::parse_float(value, v)) {
                    throw std::runtime_error("Parameter '" + name + "' must be float on device '" + dev_name + "' (" + classname + ")");
                }
                if (spec.min.has_value() && static_cast<double>(v) < *spec.min) {
                    throw std::runtime_error("Parameter '" + name + "' below min on device '" + dev_name + "' (" + classname + ")");
                }
                if (spec.max.has_value() && static_cast<double>(v) > *spec.max) {
                    throw std::runtime_error("Parameter '" + name + "' above max on device '" + dev_name + "' (" + classname + ")");
                }
                break;
            }
            case ParamSchemaType::Int: {
                long long v = 0;
                if (!locale_safe::parse_int64(value, v)) {
                    throw std::runtime_error("Parameter '" + name + "' must be int on device '" + dev_name + "' (" + classname + ")");
                }
                if (spec.min.has_value() && static_cast<double>(v) < *spec.min) {
                    throw std::runtime_error("Parameter '" + name + "' below min on device '" + dev_name + "' (" + classname + ")");
                }
                if (spec.max.has_value() && static_cast<double>(v) > *spec.max) {
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

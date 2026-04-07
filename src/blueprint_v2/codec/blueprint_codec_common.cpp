#include "blueprint_codec_internal.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace bp2::codec_detail {

// ============================================================
// Core parsing helpers
// ============================================================

float parse_finite_float(nlohmann::json const& value, std::string const& field_name) {
    if (!value.is_number()) {
        throw std::runtime_error("invalid numeric field: " + field_name);
    }
    const float v = value.get<float>();
    if (!std::isfinite(v)) {
        throw std::runtime_error("invalid non-finite numeric field: " + field_name);
    }
    return v;
}

bool parse_number_string(std::string const& s, float& out) {
    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(s.c_str(), &end);
    const bool parsed_ok = (end != s.c_str() && *end == '\0' && errno != ERANGE);
    if (!parsed_ok || !std::isfinite(parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

bool parse_bool_string(std::string const& s, std::string& normalized) {
    if (s == "true" || s == "1") {
        normalized = "true";
        return true;
    }
    if (s == "false" || s == "0") {
        normalized = "false";
        return true;
    }
    return false;
}

bool parse_vec2_string(std::string const& s) {
    const auto comma = s.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    const std::string lhs = s.substr(0, comma);
    const std::string rhs = s.substr(comma + 1);
    float x = 0.0f;
    float y = 0.0f;
    return parse_number_string(lhs, x) && parse_number_string(rhs, y);
}

std::string float_to_string(float v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

// ============================================================
// Port type conversion — single source of truth
// ============================================================

/// Canonical table of PortType ↔ string name, used by all conversions.
struct PortTypeEntry {
    PortType type;
    std::string_view name;
};

static constexpr PortTypeEntry port_type_table[] = {
    {PortType::V,           "V"},
    {PortType::I,           "I"},
    {PortType::Bool,        "Bool"},
    {PortType::RPM,         "RPM"},
    {PortType::Temperature, "Temperature"},
    {PortType::Pressure,    "Pressure"},
    {PortType::Position,    "Position"},
    {PortType::Any,         "Any"},
};

std::string port_type_to_string(PortType t) {
    for (const auto& e : port_type_table) {
        if (e.type == t) return std::string(e.name);
    }
    return "Any";
}

std::optional<PortType> port_type_from_name(std::string_view s) {
    for (const auto& e : port_type_table) {
        if (e.name == s) return e.type;
    }
    return std::nullopt;
}

bool is_known_port_type_value(int v) {
    for (const auto& e : port_type_table) {
        if (static_cast<int>(e.type) == v) return true;
    }
    return false;
}

std::string domain_to_string(Domain d) {
    switch (d) {
        case Domain::Electrical: return "Electrical";
        case Domain::Logical: return "Logical";
        case Domain::Mechanical: return "Mechanical";
        case Domain::Hydraulic: return "Hydraulic";
        case Domain::Thermal: return "Thermal";
    }
    return "Electrical";
}

// ============================================================
// Typed param assignment
// ============================================================

void assign_param_by_descriptor(Blueprint::Node& node,
                                ui::StringInterner& interner,
                                std::string const& key,
                                nlohmann::json const& val,
                                ParamSchemaEntry const& schema,
                                TypeDefinition const* type_def) {
    const auto key_iid = interner.intern(key);
    switch (schema.type) {
        case ParamSchemaType::Float:
        case ParamSchemaType::Int: {
            if (val.is_number()) {
                node.params[key_iid] = parse_finite_float(val, "params." + key);
                return;
            }
            if (val.is_string()) {
                float parsed = 0.0f;
                if (!parse_number_string(val.get<std::string>(), parsed)) {
                    throw std::runtime_error("invalid node entry: param '" + key + "' must be number");
                }
                node.params[key_iid] = parsed;
                return;
            }
            throw std::runtime_error("invalid node entry: param '" + key + "' must be number");
        }
        case ParamSchemaType::Bool: {
            if (val.is_boolean()) {
                node.string_params[key] = val.get<bool>() ? "true" : "false";
                return;
            }
            if (val.is_string()) {
                std::string normalized;
                if (!parse_bool_string(val.get<std::string>(), normalized)) {
                    throw std::runtime_error("invalid node entry: param '" + key + "' must be bool");
                }
                node.string_params[key] = std::move(normalized);
                return;
            }
            throw std::runtime_error("invalid node entry: param '" + key + "' must be bool");
        }
        case ParamSchemaType::String: {
            if (!val.is_string()) {
                throw std::runtime_error("invalid node entry: param '" + key + "' must be string");
            }
            node.string_params[key] = val.get<std::string>();
            return;
        }
    }

    (void)type_def;
}

// ============================================================
// Decode field helpers
// ============================================================

void check_allowed_fields(nlohmann::json const& obj,
                          std::unordered_set<std::string> const& allowed,
                          std::string const& context) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (allowed.find(it.key()) == allowed.end()) {
            throw std::runtime_error("unknown " + context + " field: " + it.key());
        }
    }
}

void require_field(nlohmann::json const& obj,
                   std::string const& field,
                   bool (nlohmann::json::*type_check)() const,
                   std::string const& context,
                   std::string const& type_name) {
    if (!obj.contains(field) || !(obj[field].*type_check)()) {
        throw std::runtime_error(context + ": missing " + type_name + " field '" + field + "'");
    }
}

std::optional<std::string> read_optional_string(nlohmann::json const& obj,
                                                 std::string const& field,
                                                 std::string const& context) {
    if (!obj.contains(field)) return std::nullopt;
    if (!obj[field].is_string()) {
        throw std::runtime_error(context + ": " + field + " must be string");
    }
    return obj[field].get<std::string>();
}

std::optional<bool> read_optional_bool(nlohmann::json const& obj,
                                        std::string const& field,
                                        std::string const& context) {
    if (!obj.contains(field)) return std::nullopt;
    if (!obj[field].is_boolean()) {
        throw std::runtime_error(context + ": " + field + " must be boolean");
    }
    return obj[field].get<bool>();
}

std::optional<float> read_optional_float(nlohmann::json const& obj,
                                          std::string const& field,
                                          std::string const& context) {
    if (!obj.contains(field)) return std::nullopt;
    if (!obj[field].is_number()) {
        throw std::runtime_error(context + ": " + field + " must be numeric");
    }
    return parse_finite_float(obj[field], field);
}

} // namespace bp2::codec_detail

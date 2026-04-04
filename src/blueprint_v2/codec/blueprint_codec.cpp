#include "blueprint_codec.h"
#include "blueprint_v2/validation/invariant_checker.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace bp2 {

namespace {

static float parse_finite_float(nlohmann::json const& value, std::string const& field_name) {
    if (!value.is_number()) {
        throw std::runtime_error("invalid numeric field: " + field_name);
    }
    const float v = value.get<float>();
    if (!std::isfinite(v)) {
        throw std::runtime_error("invalid non-finite numeric field: " + field_name);
    }
    return v;
}

static bool parse_number_string(std::string const& s, float& out) {
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

static bool parse_bool_string(std::string const& s, std::string& normalized) {
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

static bool parse_vec2_string(std::string const& s) {
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

static std::string float_to_string(float v) {
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

static std::string port_type_to_string(PortType t) {
    switch (t) {
        case PortType::V: return "V";
        case PortType::I: return "I";
        case PortType::Bool: return "Bool";
        case PortType::RPM: return "RPM";
        case PortType::Temperature: return "Temperature";
        case PortType::Pressure: return "Pressure";
        case PortType::Position: return "Position";
        case PortType::Any: return "Any";
    }
    return "Any";
}

static std::string domain_to_string(Domain d) {
    switch (d) {
        case Domain::Electrical: return "Electrical";
        case Domain::Logical: return "Logical";
        case Domain::Mechanical: return "Mechanical";
        case Domain::Hydraulic: return "Hydraulic";
        case Domain::Thermal: return "Thermal";
    }
    return "Electrical";
}

static void assign_param_by_descriptor(Blueprint::Node& node,
                                       ui::StringInterner& interner,
                                       std::string const& key,
                                       nlohmann::json const& val,
                                       TypeRegistry::ParamDescriptor const& desc) {
    const auto key_iid = interner.intern(key);
    switch (desc.kind) {
        case TypeRegistry::ParamKind::Number: {
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
        case TypeRegistry::ParamKind::Bool: {
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
        case TypeRegistry::ParamKind::Enum: {
            if (!val.is_string()) {
                throw std::runtime_error("invalid node entry: enum param '" + key + "' must be string");
            }
            const std::string enum_v = val.get<std::string>();
            const bool allowed = std::find(desc.enum_values.begin(), desc.enum_values.end(), enum_v)
                != desc.enum_values.end();
            if (!allowed) {
                throw std::runtime_error("invalid node entry: enum param '" + key + "' value not allowed");
            }
            node.string_params[key] = enum_v;
            return;
        }
        case TypeRegistry::ParamKind::Table:
        case TypeRegistry::ParamKind::String: {
            if (!val.is_string()) {
                throw std::runtime_error("invalid node entry: param '" + key + "' must be string");
            }
            node.string_params[key] = val.get<std::string>();
            return;
        }
        case TypeRegistry::ParamKind::Vec2: {
            if (!val.is_string()) {
                throw std::runtime_error("invalid node entry: vec2 param '" + key + "' must be string");
            }
            const std::string vec = val.get<std::string>();
            if (!parse_vec2_string(vec)) {
                throw std::runtime_error("invalid node entry: vec2 param '" + key + "' format invalid");
            }
            node.string_params[key] = vec;
            return;
        }
    }
}

static bool is_known_port_type_value(int v) {
    return v == static_cast<int>(PortType::V)
        || v == static_cast<int>(PortType::I)
        || v == static_cast<int>(PortType::Bool)
        || v == static_cast<int>(PortType::RPM)
        || v == static_cast<int>(PortType::Temperature)
        || v == static_cast<int>(PortType::Pressure)
        || v == static_cast<int>(PortType::Position)
        || v == static_cast<int>(PortType::Any);
}

/// Infer bp2 Domain from editor PortType so that decoded nodes carry a
/// self-contained Interface usable by the path resolver.
static Domain domain_from_port_type(PortType t) {
    switch (t) {
        case PortType::V:
        case PortType::I:
        case PortType::Any:
            return Domain::Electrical;
        case PortType::Bool:
            return Domain::Logical;
        case PortType::RPM:
        case PortType::Position:
            return Domain::Mechanical;
        case PortType::Pressure:
            return Domain::Hydraulic;
        case PortType::Temperature:
            return Domain::Thermal;
    }
    return Domain::Electrical;
}

/// Convert editor PortSide to bp2 Direction.
static Direction direction_from_port_side(PortSide s) {
    switch (s) {
        case PortSide::Input:  return Direction::Input;
        case PortSide::Output: return Direction::Output;
        case PortSide::InOut:  return Direction::InOut;
    }
    return Direction::Output;
}

static bool is_default_node_content(const Blueprint::Node& node) {
    return node.content_type == NodeContentType::None
        && node.content_label.empty()
        && node.content_value == 0.0f
        && node.content_min == 0.0f
        && node.content_max == 1.0f
        && node.content_unit.empty()
        && !node.content_state
        && !node.content_tripped;
}

static nlohmann::json encode_node_ports(const Blueprint::Node& node,
                                        ui::StringInterner const& interner) {
    nlohmann::json ports = nlohmann::json::object();

    std::unordered_map<ui::InternedId, bp2::Direction> dirs;
    std::unordered_map<ui::InternedId, PortType> types;

    for (auto const& p : node.inputs) {
        auto it = dirs.find(p.name);
        if (it == dirs.end()) {
            dirs[p.name] = bp2::Direction::Input;
            types[p.name] = p.type;
        } else if (it->second == bp2::Direction::Output) {
            it->second = bp2::Direction::InOut;
        }
    }
    for (auto const& p : node.outputs) {
        auto it = dirs.find(p.name);
        if (it == dirs.end()) {
            dirs[p.name] = bp2::Direction::Output;
            types[p.name] = p.type;
        } else if (it->second == bp2::Direction::Input) {
            it->second = bp2::Direction::InOut;
        }
    }

    for (auto const& [name, dir] : dirs) {
        nlohmann::json p;
        p["direction"] = (dir == bp2::Direction::Input)
            ? "In"
            : (dir == bp2::Direction::Output ? "Out" : "InOut");
        p["type"] = static_cast<int>(types[name]);
        ports[std::string(interner.resolve(name))] = std::move(p);
    }

    return ports;
}

nlohmann::json encode_interface(Interface const& iface,
                                 ui::StringInterner const& interner,
                                 TypeRegistry::Entry const* type_entry) {
    std::vector<PortDescriptor> sorted = iface.ports();
    std::sort(sorted.begin(), sorted.end(), [&](const PortDescriptor& a, const PortDescriptor& b) {
        std::string_view na = interner.resolve(a.name);
        std::string_view nb = interner.resolve(b.name);
        return na < nb;
    });

    auto arr = nlohmann::json::array();
    for (auto const& port : sorted) {
        nlohmann::json p;
        const std::string name = std::string(interner.resolve(port.name));
        p["name"] = name;
        p["domain"] = static_cast<int>(port.domain);
        p["direction"] = static_cast<int>(port.direction);
        if (type_entry) {
            auto it = type_entry->port_meta.find(name);
            if (it != type_entry->port_meta.end()) {
                const auto& meta = it->second;
                p["type"] = port_type_to_string(meta.type);
                p["source_writer"] = meta.source_writer;
            }
        }
        arr.push_back(p);
    }
    return arr;
}

nlohmann::json encode_nodes(std::vector<Blueprint::Node> const& nodes,
                             ui::StringInterner const& interner,
                             TypeRegistry const* registry) {
    std::vector<Blueprint::Node const*> sorted;
    sorted.reserve(nodes.size());
    for (auto const& node : nodes) sorted.push_back(&node);
    std::sort(sorted.begin(), sorted.end(), [&](Blueprint::Node const* a, Blueprint::Node const* b) {
        std::string_view ida = interner.resolve(a->id);
        std::string_view idb = interner.resolve(b->id);
        if (ida == idb) return interner.resolve(a->type) < interner.resolve(b->type);
        return ida < idb;
    });

    auto arr = nlohmann::json::array();
    for (auto const* node_ptr : sorted) {
        auto const& node = *node_ptr;
        nlohmann::json n;
        n["id"] = std::string(interner.resolve(node.id));
        n["type"] = std::string(interner.resolve(node.type));
        if (!node.name.empty()) n["name"] = node.name;
        if (!node.render_hint.empty()) n["render_hint"] = node.render_hint;
        if (!node.group_id.empty()) n["group_id"] = node.group_id;
        if (node.expandable) n["expandable"] = true;
        if (!node.collapsed) n["collapsed"] = false;
        if (!node.blueprint_path.empty()) n["blueprint_path"] = node.blueprint_path;
        n["position"] = {{"x", node.x}, {"y", node.y}};
        if (node.width.has_value()) n["width"] = *node.width;
        if (node.height.has_value()) n["height"] = *node.height;

        nlohmann::json params = nlohmann::json::object();
        nlohmann::json sparams = nlohmann::json::object();
        std::unordered_set<std::string> descriptor_keys;
        const TypeRegistry::Entry* entry = registry ? registry->find(node.type) : nullptr;

        if (entry) {
            for (const auto& [key, desc] : entry->param_descriptors) {
                descriptor_keys.insert(key);
                ui::InternedId key_iid = interner.lookup(key);
                const auto pit = key_iid.empty() ? node.params.end() : node.params.find(key_iid);
                const auto sit = node.string_params.find(key);

                switch (desc.kind) {
                    case TypeRegistry::ParamKind::Number: {
                        if (pit != node.params.end()) {
                            params[key] = pit->second;
                        } else if (sit != node.string_params.end()) {
                            float parsed = 0.0f;
                            if (parse_number_string(sit->second, parsed)) {
                                params[key] = parsed;
                            }
                        }
                        break;
                    }
                    case TypeRegistry::ParamKind::Bool: {
                        if (sit != node.string_params.end()) {
                            std::string normalized;
                            if (parse_bool_string(sit->second, normalized)) {
                                params[key] = (normalized == "true");
                            }
                        } else if (pit != node.params.end()) {
                            params[key] = (pit->second != 0.0f);
                        }
                        break;
                    }
                    case TypeRegistry::ParamKind::Enum: {
                        if (sit != node.string_params.end()) {
                            const bool allowed = std::find(desc.enum_values.begin(), desc.enum_values.end(), sit->second)
                                != desc.enum_values.end();
                            if (allowed) {
                                params[key] = sit->second;
                            }
                        }
                        break;
                    }
                    case TypeRegistry::ParamKind::Vec2: {
                        if (sit != node.string_params.end() && parse_vec2_string(sit->second)) {
                            params[key] = sit->second;
                        }
                        break;
                    }
                    case TypeRegistry::ParamKind::Table:
                    case TypeRegistry::ParamKind::String: {
                        if (sit != node.string_params.end()) {
                            params[key] = sit->second;
                        } else if (pit != node.params.end()) {
                            params[key] = float_to_string(pit->second);
                        }
                        break;
                    }
                }
            }
        }

        for (auto const& [k, v] : node.params) {
            std::string key = std::string(interner.resolve(k));
            if (descriptor_keys.find(key) != descriptor_keys.end()) {
                continue;
            }
            params[key] = v;
        }
        for (auto const& [k, v] : node.string_params) {
            if (descriptor_keys.find(k) != descriptor_keys.end()) {
                continue;
            }
            sparams[k] = v;
        }

        if (!params.empty()) {
            n["params"] = params;
        }
        if (!sparams.empty()) {
            n["string_params"] = sparams;
        }

        if (!node.inputs.empty() || !node.outputs.empty()) {
            n["ports"] = encode_node_ports(node, interner);
        }

        if (!node.layout_overrides.empty()) {
            nlohmann::json los = nlohmann::json::array();
            for (auto const& lo : node.layout_overrides) {
                nlohmann::json jlo;
                jlo["port_name"] = lo.port_name;
                if (lo.side.has_value()) jlo["side"] = *lo.side;
                if (lo.position.has_value()) jlo["position"] = *lo.position;
                los.push_back(std::move(jlo));
            }
            n["layout_overrides"] = std::move(los);
        }

        if (!is_default_node_content(node)) {
            n["content_type"] = static_cast<int>(node.content_type);
            n["content_label"] = node.content_label;
            n["content_value"] = node.content_value;
            n["content_min"] = node.content_min;
            n["content_max"] = node.content_max;
            n["content_unit"] = node.content_unit;
            n["content_state"] = node.content_state;
            n["content_tripped"] = node.content_tripped;
        }

        if (node.has_color) {
            n["has_color"] = true;
            n["color_r"] = node.color_r;
            n["color_g"] = node.color_g;
            n["color_b"] = node.color_b;
            n["color_a"] = node.color_a;
        }

        arr.push_back(n);
    }
    return arr;
}

nlohmann::json encode_wires(std::vector<Blueprint::Wire> const& wires,
                             ui::StringInterner const& interner,
                             PathArena const& path_arena) {
    auto arr = nlohmann::json::array();
    for (auto const& wire : wires) {
        nlohmann::json w;
        w["id"] = std::string(interner.resolve(wire.id));
        w["source"] = path_arena.to_string(wire.source);
        w["target"] = path_arena.to_string(wire.target);
        if (!wire.routing_points.empty()) {
            nlohmann::json rp = nlohmann::json::array();
            for (auto const& [x, y] : wire.routing_points) {
                rp.push_back({x, y});
            }
            w["routing_points"] = std::move(rp);
        }
        arr.push_back(w);
    }
    return arr;
}

nlohmann::json encode_nested(std::vector<Blueprint::Nested> const& nested_vec,
                                ui::StringInterner const& interner,
                                PathArena const& arena,
                                TypeRegistry const* registry) {
    std::vector<Blueprint::Nested const*> sorted;
    sorted.reserve(nested_vec.size());
    for (auto const& nested : nested_vec) sorted.push_back(&nested);
    std::sort(sorted.begin(), sorted.end(), [&](Blueprint::Nested const* a, Blueprint::Nested const* b) {
        std::string_view ida = interner.resolve(a->id);
        std::string_view idb = interner.resolve(b->id);
        return ida < idb;
    });

    auto arr = nlohmann::json::array();
    for (auto const* nested_ptr : sorted) {
        auto const& nested = *nested_ptr;
        nlohmann::json n;
        n["id"] = std::string(interner.resolve(nested.id));
        n["blueprint"] = std::string(interner.resolve(nested.blueprint_id));
        n["embedded"] = nested.embedded;
        n["position"] = {{"x", nested.x}, {"y", nested.y}};
        if (nested.embedded && nested.inline_def) {
            n["definition"] = nlohmann::json::parse(
                BlueprintCodec::encode(*nested.inline_def, interner, arena, registry)
            );
        }
        arr.push_back(n);
    }
    return arr;
}

Interface decode_interface(nlohmann::json const& arr,
                           ui::StringInterner& interner) {
    std::vector<PortDescriptor> ports;
    for (auto const& p : arr) {
        if (!p.is_object()) {
            throw std::runtime_error("invalid interface entry: expected object");
        }
        static const std::unordered_set<std::string> allowed_interface_fields = {
            "name", "domain", "direction", "type", "source_writer"
        };
        for (auto it = p.begin(); it != p.end(); ++it) {
            if (allowed_interface_fields.find(it.key()) == allowed_interface_fields.end()) {
                throw std::runtime_error("unknown interface field: " + it.key());
            }
        }
        if (!p.contains("name") || !p["name"].is_string()) {
            throw std::runtime_error("invalid interface entry: missing string field 'name'");
        }
        if (!p.contains("domain") || !p["domain"].is_number_integer()) {
            throw std::runtime_error("invalid interface entry: missing integer field 'domain'");
        }
        if (!p.contains("direction") || !p["direction"].is_number_integer()) {
            throw std::runtime_error("invalid interface entry: missing integer field 'direction'");
        }

        const int domain_v = p["domain"].get<int>();
        if (domain_v != static_cast<int>(Domain::Electrical)
            && domain_v != static_cast<int>(Domain::Logical)
            && domain_v != static_cast<int>(Domain::Mechanical)
            && domain_v != static_cast<int>(Domain::Hydraulic)
            && domain_v != static_cast<int>(Domain::Thermal)) {
            throw std::runtime_error("invalid interface entry: unknown domain value");
        }

        const int direction_v = p["direction"].get<int>();
        if (direction_v != static_cast<int>(Direction::Input)
            && direction_v != static_cast<int>(Direction::Output)
            && direction_v != static_cast<int>(Direction::InOut)) {
            throw std::runtime_error("invalid interface entry: unknown direction value");
        }

        PortDescriptor pd;
        pd.name = interner.intern(p["name"].get<std::string>());
        pd.domain = static_cast<Domain>(domain_v);
        pd.direction = static_cast<Direction>(direction_v);
        ports.push_back(pd);
    }
    return Interface(std::move(ports));
}

static PortType parse_port_type_name(std::string_view s) {
    if (s == "V") return PortType::V;
    if (s == "I") return PortType::I;
    if (s == "Bool") return PortType::Bool;
    if (s == "RPM") return PortType::RPM;
    if (s == "Temperature") return PortType::Temperature;
    if (s == "Pressure") return PortType::Pressure;
    if (s == "Position") return PortType::Position;
    return PortType::Any;
}

static bool is_known_port_type_name(std::string_view s) {
    return s == "V" || s == "I" || s == "Bool" || s == "RPM"
        || s == "Temperature" || s == "Pressure" || s == "Position" || s == "Any";
}

Blueprint decode_nodes(Blueprint bp, nlohmann::json const& arr,
                       ui::StringInterner& interner,
                       TypeRegistry const& registry) {
    for (auto const& n : arr) {
        if (!n.is_object()) {
            throw std::runtime_error("invalid node entry: expected object");
        }
        if (!n.contains("id") || !n["id"].is_string()) {
            throw std::runtime_error("invalid node entry: missing string field 'id'");
        }
        if (!n.contains("type") || !n["type"].is_string()) {
            throw std::runtime_error("invalid node entry: missing string field 'type'");
        }

        static const std::unordered_set<std::string> allowed_node_fields = {
            "id", "type", "name", "render_hint", "group_id", "expandable", "collapsed",
            "blueprint_path", "position", "width", "height", "params", "string_params",
            "ports", "layout_overrides", "content_type", "content_label", "content_value",
            "content_min", "content_max", "content_unit", "content_state", "content_tripped",
            "has_color", "color_r", "color_g", "color_b", "color_a"
        };
        for (auto it = n.begin(); it != n.end(); ++it) {
            if (allowed_node_fields.find(it.key()) == allowed_node_fields.end()) {
                throw std::runtime_error("unknown node field: " + it.key());
            }
        }

        Blueprint::Node node;
        node.id = interner.intern(n["id"].get<std::string>());
        node.type = interner.intern(n["type"].get<std::string>());
        if (n.contains("name") && !n["name"].is_string()) {
            throw std::runtime_error("invalid node entry: name must be string");
        }
        if (n.contains("name")) {
            node.name = n["name"].get<std::string>();
        }
        if (n.contains("render_hint") && !n["render_hint"].is_string()) {
            throw std::runtime_error("invalid node entry: render_hint must be string");
        }
        if (n.contains("render_hint")) {
            node.render_hint = n["render_hint"].get<std::string>();
        }
        if (n.contains("group_id") && !n["group_id"].is_string()) {
            throw std::runtime_error("invalid node entry: group_id must be string");
        }
        if (n.contains("group_id")) {
            node.group_id = n["group_id"].get<std::string>();
        }
        if (n.contains("expandable") && !n["expandable"].is_boolean()) {
            throw std::runtime_error("invalid node entry: expandable must be boolean");
        }
        if (n.contains("expandable")) {
            node.expandable = n["expandable"].get<bool>();
        }
        if (n.contains("collapsed") && !n["collapsed"].is_boolean()) {
            throw std::runtime_error("invalid node entry: collapsed must be boolean");
        }
        if (n.contains("collapsed")) {
            node.collapsed = n["collapsed"].get<bool>();
        }
        if (n.contains("blueprint_path") && !n["blueprint_path"].is_string()) {
            throw std::runtime_error("invalid node entry: blueprint_path must be string");
        }
        if (n.contains("blueprint_path")) {
            node.blueprint_path = n["blueprint_path"].get<std::string>();
        }
        if (n.contains("position")) {
            if (!n["position"].is_object()) {
                throw std::runtime_error("invalid node entry: 'position' must be an object");
            }
            if (!n["position"].contains("x") || !n["position"]["x"].is_number()) {
                throw std::runtime_error("invalid node entry: missing numeric field 'position.x'");
            }
            if (!n["position"].contains("y") || !n["position"]["y"].is_number()) {
                throw std::runtime_error("invalid node entry: missing numeric field 'position.y'");
            }
            node.x = parse_finite_float(n["position"]["x"], "position.x");
            node.y = parse_finite_float(n["position"]["y"], "position.y");
        } else {
            node.x = 0.0f;
            node.y = 0.0f;
        }
        if (n.contains("width") && !n["width"].is_number()) {
            throw std::runtime_error("invalid node entry: width must be numeric");
        }
        if (n.contains("width")) {
            node.width = parse_finite_float(n["width"], "width");
        }
        if (n.contains("height") && !n["height"].is_number()) {
            throw std::runtime_error("invalid node entry: height must be numeric");
        }
        if (n.contains("height")) {
            node.height = parse_finite_float(n["height"], "height");
        }
        const TypeRegistry::Entry* type_entry = registry.find(node.type);

        if (n.contains("params") && n["params"].is_object()) {
            for (auto& [key, val] : n["params"].items()) {
                if (type_entry) {
                    auto dit = type_entry->param_descriptors.find(key);
                    if (dit != type_entry->param_descriptors.end()) {
                        assign_param_by_descriptor(node, interner, key, val, dit->second);
                        continue;
                    }
                }

                if (val.is_number()) {
                    node.params[interner.intern(key)] = parse_finite_float(val, "params." + key);
                    continue;
                }
                if (val.is_string()) {
                    const std::string s = val.get<std::string>();
                    float parsed = 0.0f;
                    if (parse_number_string(s, parsed)) {
                        node.params[interner.intern(key)] = parsed;
                    } else {
                        node.string_params[key] = s;
                    }
                    continue;
                }

                throw std::runtime_error("invalid node entry: params values must be number or string");
            }
        } else if (n.contains("params") && !n["params"].is_object()) {
            throw std::runtime_error("invalid node entry: params must be an object");
        }
        if (n.contains("string_params") && n["string_params"].is_object()) {
            for (auto& [key, val] : n["string_params"].items()) {
                if (!val.is_string()) {
                    throw std::runtime_error("invalid node entry: string_params values must be string");
                }
                node.string_params[key] = val.get<std::string>();
            }
        } else if (n.contains("string_params") && !n["string_params"].is_object()) {
            throw std::runtime_error("invalid node entry: string_params must be an object");
        }

        if (type_entry) {
            for (const auto& [k, v] : type_entry->param_defaults) {
                const auto key_iid = interner.intern(k);
                if (node.params.find(key_iid) != node.params.end()) {
                    continue;
                }
                if (node.string_params.find(k) != node.string_params.end()) {
                    continue;
                }

                auto dit = type_entry->param_descriptors.find(k);
                if (dit != type_entry->param_descriptors.end()) {
                    const auto& desc = dit->second;
                    switch (desc.kind) {
                        case TypeRegistry::ParamKind::Number: {
                            float parsed = 0.0f;
                            if (parse_number_string(v, parsed)) {
                                node.params[key_iid] = parsed;
                            } else {
                                throw std::runtime_error("invalid default for numeric param '" + k + "'");
                            }
                            break;
                        }
                        case TypeRegistry::ParamKind::Bool: {
                            std::string normalized;
                            if (parse_bool_string(v, normalized)) {
                                node.string_params[k] = std::move(normalized);
                            } else {
                                throw std::runtime_error("invalid default for bool param '" + k + "'");
                            }
                            break;
                        }
                        case TypeRegistry::ParamKind::Enum: {
                            const bool allowed = std::find(desc.enum_values.begin(), desc.enum_values.end(), v)
                                != desc.enum_values.end();
                            if (!allowed) {
                                throw std::runtime_error("invalid default for enum param '" + k + "'");
                            }
                            node.string_params[k] = v;
                            break;
                        }
                        case TypeRegistry::ParamKind::Vec2: {
                            if (!parse_vec2_string(v)) {
                                throw std::runtime_error("invalid default for vec2 param '" + k + "'");
                            }
                            node.string_params[k] = v;
                            break;
                        }
                        case TypeRegistry::ParamKind::Table:
                        case TypeRegistry::ParamKind::String:
                            node.string_params[k] = v;
                            break;
                    }
                    continue;
                }

                float parsed = 0.0f;
                if (parse_number_string(v, parsed)) {
                    node.params[key_iid] = parsed;
                } else {
                    node.string_params[k] = v;
                }
            }
        }

        if (n.contains("content_type")) {
            if (!n["content_type"].is_number_integer()) {
                throw std::runtime_error("invalid node entry: content_type must be integer");
            }
            int ct = n["content_type"].get<int>();
            if (ct < 0 || ct > static_cast<int>(NodeContentType::Knob)) {
                throw std::runtime_error("invalid node entry: content_type out of range");
            }
            node.content_type = static_cast<NodeContentType>(ct);
        }
        if (n.contains("content_label") && !n["content_label"].is_string()) {
            throw std::runtime_error("invalid node entry: content_label must be string");
        }
        if (n.contains("content_label")) {
            node.content_label = n["content_label"].get<std::string>();
        }
        if (n.contains("content_value") && !n["content_value"].is_number()) {
            throw std::runtime_error("invalid node entry: content_value must be numeric");
        }
        if (n.contains("content_value")) {
            node.content_value = parse_finite_float(n["content_value"], "content_value");
        }
        if (n.contains("content_min") && !n["content_min"].is_number()) {
            throw std::runtime_error("invalid node entry: content_min must be numeric");
        }
        if (n.contains("content_min")) {
            node.content_min = parse_finite_float(n["content_min"], "content_min");
        }
        if (n.contains("content_max") && !n["content_max"].is_number()) {
            throw std::runtime_error("invalid node entry: content_max must be numeric");
        }
        if (n.contains("content_max")) {
            node.content_max = parse_finite_float(n["content_max"], "content_max");
        }
        if (node.content_min > node.content_max) {
            throw std::runtime_error("invalid node entry: content_min must be <= content_max");
        }
        if (n.contains("content_unit") && !n["content_unit"].is_string()) {
            throw std::runtime_error("invalid node entry: content_unit must be string");
        }
        if (n.contains("content_unit")) {
            node.content_unit = n["content_unit"].get<std::string>();
        }
        if (n.contains("content_state") && !n["content_state"].is_boolean()) {
            throw std::runtime_error("invalid node entry: content_state must be boolean");
        }
        if (n.contains("content_state")) {
            node.content_state = n["content_state"].get<bool>();
        }
        if (n.contains("content_tripped") && !n["content_tripped"].is_boolean()) {
            throw std::runtime_error("invalid node entry: content_tripped must be boolean");
        }
        if (n.contains("content_tripped")) {
            node.content_tripped = n["content_tripped"].get<bool>();
        }

        if (n.contains("has_color") && !n["has_color"].is_boolean()) {
            throw std::runtime_error("invalid node entry: has_color must be boolean");
        }
        if (n.contains("has_color")) {
            node.has_color = n["has_color"].get<bool>();
            if (node.has_color) {
                if (n.contains("color_r") && !n["color_r"].is_number()) {
                    throw std::runtime_error("invalid node entry: color_r must be numeric");
                }
                if (n.contains("color_g") && !n["color_g"].is_number()) {
                    throw std::runtime_error("invalid node entry: color_g must be numeric");
                }
                if (n.contains("color_b") && !n["color_b"].is_number()) {
                    throw std::runtime_error("invalid node entry: color_b must be numeric");
                }
                if (n.contains("color_a") && !n["color_a"].is_number()) {
                    throw std::runtime_error("invalid node entry: color_a must be numeric");
                }
                if (n.contains("color_r")) node.color_r = parse_finite_float(n["color_r"], "color_r");
                if (n.contains("color_g")) node.color_g = parse_finite_float(n["color_g"], "color_g");
                if (n.contains("color_b")) node.color_b = parse_finite_float(n["color_b"], "color_b");
                if (n.contains("color_a")) node.color_a = parse_finite_float(n["color_a"], "color_a");
            }
        }

        if (n.contains("layout_overrides") && n["layout_overrides"].is_array()) {
            for (auto const& lo : n["layout_overrides"]) {
                if (!lo.is_object()) {
                    throw std::runtime_error("invalid node entry: layout_overrides item must be an object");
                }
                static const std::unordered_set<std::string> allowed_layout_override_fields = {
                    "port_name", "side", "position"
                };
                for (auto it = lo.begin(); it != lo.end(); ++it) {
                    if (allowed_layout_override_fields.find(it.key()) == allowed_layout_override_fields.end()) {
                        throw std::runtime_error("unknown layout_overrides field: " + it.key());
                    }
                }
                if (!lo.contains("port_name") || !lo["port_name"].is_string()) {
                    throw std::runtime_error("invalid node entry: layout_overrides missing string field 'port_name'");
                }
                Blueprint::Node::PortLayoutOverride ov;
                ov.port_name = lo["port_name"].get<std::string>();
                if (lo.contains("side") && lo["side"].is_string()) {
                    std::string side = lo["side"].get<std::string>();
                    if (side != "left" && side != "right" && side != "top" && side != "bottom") {
                        throw std::runtime_error("invalid node entry: layout_overrides has unknown side");
                    }
                    ov.side = std::move(side);
                }
                if (lo.contains("position") && lo["position"].is_number_integer()) {
                    ov.position = lo["position"].get<int>();
                } else if (lo.contains("position") && !lo["position"].is_number_integer()) {
                    throw std::runtime_error("invalid node entry: layout_overrides.position must be integer");
                }
                node.layout_overrides.push_back(std::move(ov));
            }
        }

        if (n.contains("ports") && n["ports"].is_object()) {
            for (auto const& [port_name, p] : n["ports"].items()) {
                if (!p.is_object()) {
                    throw std::runtime_error("invalid node entry: port descriptor must be an object");
                }
                static const std::unordered_set<std::string> allowed_port_fields = {
                    "direction", "type"
                };
                for (auto it = p.begin(); it != p.end(); ++it) {
                    if (allowed_port_fields.find(it.key()) == allowed_port_fields.end()) {
                        throw std::runtime_error("unknown node port field: " + it.key());
                    }
                }
                auto pid = interner.intern(port_name);

                PortType ptype = PortType::Any;
                if (p.contains("type")) {
                    if (p["type"].is_number_integer()) {
                        const int type_v = p["type"].get<int>();
                        if (!is_known_port_type_value(type_v)) {
                            throw std::runtime_error("invalid node entry: unknown port type value");
                        }
                        ptype = static_cast<PortType>(type_v);
                    } else if (p["type"].is_string()) {
                        std::string type_s = p["type"].get<std::string>();
                        if (!is_known_port_type_name(type_s)) {
                            throw std::runtime_error("invalid node entry: unknown port type string");
                        }
                        ptype = parse_port_type_name(type_s);
                    } else {
                        throw std::runtime_error("invalid node entry: port type must be int or string");
                    }
                }

                std::string dir = "Out";
                if (p.contains("direction")) {
                    if (!p["direction"].is_string()) {
                        throw std::runtime_error("invalid node entry: port direction must be string");
                    }
                    dir = p["direction"].get<std::string>();
                }
                if (dir == "In") {
                    node.inputs.emplace_back(pid, PortSide::Input, ptype);
                } else if (dir == "Out") {
                    node.outputs.emplace_back(pid, PortSide::Output, ptype);
                } else if (dir == "InOut") {
                    node.inputs.emplace_back(pid, PortSide::InOut, ptype);
                    node.outputs.emplace_back(pid, PortSide::InOut, ptype);
                } else {
                    throw std::runtime_error("invalid node entry: unknown port direction");
                }
            }
        } else if (n.contains("ports") && !n["ports"].is_object()) {
            throw std::runtime_error("invalid node entry: ports must be an object");
        }

        // Build node.iface from decoded EditorPorts so that PathResolver can
        // resolve wire endpoints against the node's own interface, even when
        // the node type is not in the library registry (e.g. embedded
        // blueprint proxy nodes with a custom type name).
        {
            std::unordered_map<ui::InternedId, PortDescriptor> merged;
            for (auto const& ep : node.inputs) {
                auto it = merged.find(ep.name);
                if (it == merged.end()) {
                    merged[ep.name] = {ep.name, domain_from_port_type(ep.type),
                                       direction_from_port_side(ep.side)};
                } else if (it->second.direction != Direction::InOut) {
                    it->second.direction = Direction::InOut;
                }
            }
            for (auto const& ep : node.outputs) {
                auto it = merged.find(ep.name);
                if (it == merged.end()) {
                    merged[ep.name] = {ep.name, domain_from_port_type(ep.type),
                                       direction_from_port_side(ep.side)};
                } else if (it->second.direction != Direction::InOut) {
                    it->second.direction = Direction::InOut;
                }
            }
            std::vector<PortDescriptor> iface_ports;
            iface_ports.reserve(merged.size());
            for (auto& [_, pd] : merged) {
                iface_ports.push_back(std::move(pd));
            }
            node.iface = Interface(std::move(iface_ports));
        }

        bp = bp.with_node(std::move(node));
    }
    return bp;
}

Blueprint decode_wires(Blueprint bp, nlohmann::json const& arr,
                        ui::StringInterner& interner,
                        PathArena& arena) {
    for (auto const& w : arr) {
        if (!w.is_object()) {
            throw std::runtime_error("invalid wire entry: expected object");
        }
        if (!w.contains("id") || !w["id"].is_string()) {
            throw std::runtime_error("invalid wire entry: missing string field 'id'");
        }
        if (!w.contains("source") || !w["source"].is_string()) {
            throw std::runtime_error("invalid wire entry: missing string field 'source'");
        }
        if (!w.contains("target") || !w["target"].is_string()) {
            throw std::runtime_error("invalid wire entry: missing string field 'target'");
        }

        static const std::unordered_set<std::string> allowed_wire_fields = {
            "id", "source", "target", "routing_points"
        };
        for (auto it = w.begin(); it != w.end(); ++it) {
            if (allowed_wire_fields.find(it.key()) == allowed_wire_fields.end()) {
                throw std::runtime_error("unknown wire field: " + it.key());
            }
        }

        Blueprint::Wire wire;
        wire.id = interner.intern(w["id"].get<std::string>());
        auto src = arena.parse(w["source"].get<std::string>());
        auto tgt = arena.parse(w["target"].get<std::string>());
        if (!src || !tgt) {
            throw std::runtime_error("invalid wire entry: endpoint path parse failed");
        }
        wire.source = *src;
        wire.target = *tgt;
        if (w.contains("routing_points") && w["routing_points"].is_array()) {
            for (auto const& rp : w["routing_points"]) {
                if (!rp.is_array() || rp.size() != 2) {
                    throw std::runtime_error("invalid wire entry: routing_points must be [x,y] pairs");
                }
                if (!rp[0].is_number() || !rp[1].is_number()) {
                    throw std::runtime_error("invalid wire entry: routing_points values must be numeric");
                }
                const float x = parse_finite_float(rp[0], "routing_points.x");
                const float y = parse_finite_float(rp[1], "routing_points.y");
                wire.routing_points.emplace_back(x, y);
            }
        } else if (w.contains("routing_points") && !w["routing_points"].is_array()) {
            throw std::runtime_error("invalid wire entry: routing_points must be an array");
        }
        bp = bp.with_wire(std::move(wire));
    }
    return bp;
}

Blueprint decode_nested(Blueprint bp, nlohmann::json const& arr,
                         ui::StringInterner& interner,
                         TypeRegistry const& registry,
                         PathArena& arena) {
    for (auto const& n : arr) {
        if (!n.is_object()) {
            throw std::runtime_error("invalid nested entry: expected object");
        }
        if (!n.contains("id") || !n["id"].is_string()) {
            throw std::runtime_error("invalid nested entry: missing string field 'id'");
        }
        if (!n.contains("blueprint") || !n["blueprint"].is_string()) {
            throw std::runtime_error("invalid nested entry: missing string field 'blueprint'");
        }

        static const std::unordered_set<std::string> allowed_nested_fields = {
            "id", "blueprint", "embedded", "position", "definition"
        };
        for (auto it = n.begin(); it != n.end(); ++it) {
            if (allowed_nested_fields.find(it.key()) == allowed_nested_fields.end()) {
                throw std::runtime_error("unknown nested field: " + it.key());
            }
        }

        Blueprint::Nested nested;
        nested.id = interner.intern(n["id"].get<std::string>());
        nested.blueprint_id = interner.intern(n["blueprint"].get<std::string>());
        nested.embedded = n.value("embedded", false);
        if (!n.contains("position") || !n["position"].is_object()) {
            throw std::runtime_error("invalid nested entry: missing object field 'position'");
        }
        if (!n["position"].contains("x") || !n["position"]["x"].is_number()) {
            throw std::runtime_error("invalid nested entry: missing numeric field 'position.x'");
        }
        if (!n["position"].contains("y") || !n["position"]["y"].is_number()) {
            throw std::runtime_error("invalid nested entry: missing numeric field 'position.y'");
        }
        nested.x = parse_finite_float(n["position"]["x"], "nested.position.x");
        nested.y = parse_finite_float(n["position"]["y"], "nested.position.y");
        if (nested.embedded && !n.contains("definition")) {
            throw std::runtime_error("invalid nested entry: embedded nested requires definition");
        }
        if (!nested.embedded && n.contains("definition")) {
            throw std::runtime_error("invalid nested entry: non-embedded nested must not contain definition");
        }
        if (nested.embedded && n.contains("definition")) {
            DecodeError inner_err;
            auto inner = BlueprintCodec::decode(
                n["definition"].dump(), interner, arena, registry, &inner_err);
            if (inner) {
                nested.inline_def = std::make_unique<Blueprint>(std::move(*inner));
                nested.iface = nested.inline_def->iface();
            } else {
                if (!inner_err.message.empty()) {
                    throw std::runtime_error(
                        "invalid nested entry: failed to decode embedded definition: "
                        + inner_err.message);
                }
                throw std::runtime_error("invalid nested entry: failed to decode embedded definition");
            }
        }
        if (!nested.embedded && !nested.blueprint_id.empty()) {
            auto* entry = registry.find(nested.blueprint_id);
            if (entry) {
                nested.iface = entry->iface;
            } else {
                throw std::runtime_error("unknown nested blueprint");
            }
        }
        bp = bp.with_nested(std::move(nested));
    }
    return bp;
}

} // anonymous namespace

std::string BlueprintCodec::encode(Blueprint const& bp,
                                    ui::StringInterner const& interner,
                                    PathArena const& arena,
                                    TypeRegistry const* registry) {
    nlohmann::json j;
    const TypeRegistry::Entry* type_entry = nullptr;
    if (registry) {
        type_entry = registry->find(bp.id());
    }

    j["version"] = "3.0";
    j["id"] = std::string(interner.resolve(bp.id()));
    j["display_name"] = bp.display_name();
    j["interface"] = encode_interface(bp.iface(), interner, type_entry);
    j["nodes"] = encode_nodes(bp.nodes(), interner, registry);
    j["wires"] = encode_wires(bp.wires(), interner, arena);
    j["nested"] = encode_nested(bp.nested(), interner, arena, registry);

    if (type_entry) {
        j["cpp_class"] = !type_entry->is_blueprint;
        j["description"] = type_entry->description;
        j["scheduler_source"] = type_entry->scheduler_source;

        nlohmann::json domains = nlohmann::json::array();
        for (Domain d : type_entry->domains) {
            domains.push_back(domain_to_string(d));
        }
        j["domains"] = std::move(domains);

        if (!type_entry->param_defaults.empty()) {
            nlohmann::json params = nlohmann::json::object();
            for (const auto& [k, v] : type_entry->param_defaults) {
                params[k] = v;
            }
            j["param_defaults"] = std::move(params);
        }
    }

    j["pan_x"] = bp.pan_x();
    j["pan_y"] = bp.pan_y();
    j["zoom"] = bp.zoom();
    j["grid_step"] = bp.grid_step();
    if (!bp.name().empty()) {
        j["name"] = bp.name();
    }
    return j.dump(2);
}

std::optional<Blueprint> BlueprintCodec::decode(
    std::string_view json_str,
    ui::StringInterner& interner,
    PathArena& arena,
    TypeRegistry const& registry,
    DecodeError* error_out) {
    try {
        auto j = nlohmann::json::parse(json_str);
        if (!j.contains("version") || !j["version"].is_string()
            || j["version"].get<std::string>() != "3.0") {
            if (error_out) {
                error_out->message = "Unsupported blueprint version (expected \"3.0\")";
            }
            return std::nullopt;
        }

        if (!j.contains("id") || !j["id"].is_string()) {
            if (error_out) error_out->message = "Missing required string field: id";
            return std::nullopt;
        }
        if (!j.contains("display_name") || !j["display_name"].is_string()) {
            if (error_out) error_out->message = "Missing required string field: display_name";
            return std::nullopt;
        }
        if (!j.contains("interface") || !j["interface"].is_array()) {
            if (error_out) error_out->message = "Missing required array field: interface";
            return std::nullopt;
        }
        if (!j.contains("nodes") || !j["nodes"].is_array()) {
            if (error_out) error_out->message = "Missing required array field: nodes";
            return std::nullopt;
        }
        if (!j.contains("wires") || !j["wires"].is_array()) {
            if (error_out) error_out->message = "Missing required array field: wires";
            return std::nullopt;
        }
        if (!j.contains("nested") || !j["nested"].is_array()) {
            if (error_out) error_out->message = "Missing required array field: nested";
            return std::nullopt;
        }

        static const std::unordered_set<std::string> allowed_top_level = {
            "version", "id", "display_name", "name", "interface", "nodes",
            "wires", "nested", "pan_x", "pan_y", "zoom", "grid_step",
            "cpp_class", "description", "domains", "scheduler_source",
            "param_defaults", "param_schema", "solver_role", "priority", "critical"
        };
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (allowed_top_level.find(it.key()) == allowed_top_level.end()) {
                if (error_out) error_out->message = "unknown top-level field: " + it.key();
                return std::nullopt;
            }
        }

        Blueprint bp;
        bp = bp.with_id(interner.intern(j["id"].get<std::string>()));
        bp = bp.with_display_name(j["display_name"].get<std::string>());
        if (j.contains("name") && !j["name"].is_string()) {
            if (error_out) error_out->message = "invalid top-level field type: name";
            return std::nullopt;
        }
        if (j.contains("name")) {
            bp = bp.with_name(j["name"].get<std::string>());
        }
        bp = bp.with_interface(decode_interface(j["interface"], interner));
        bp = decode_nodes(bp, j["nodes"], interner, registry);
        bp = decode_wires(bp, j["wires"], interner, arena);
        bp = decode_nested(bp, j["nested"], interner, registry, arena);

        auto inv = InvariantChecker::validate(bp, arena, registry);
        if (!inv.valid) {
            if (error_out) error_out->message = inv.error;
            return std::nullopt;
        }

        auto viewport_or_default = [&](const char* key, float default_value) -> float {
            if (!j.contains(key)) return default_value;
            if (!j[key].is_number()) {
                throw std::runtime_error(std::string("invalid viewport field type: ") + key);
            }
            return j[key].get<float>();
        };
        const float pan_x = viewport_or_default("pan_x", 0.0f);
        const float pan_y = viewport_or_default("pan_y", 0.0f);
        const float zoom = viewport_or_default("zoom", 1.0f);
        const float grid_step = viewport_or_default("grid_step", 16.0f);
        if (!std::isfinite(pan_x) || !std::isfinite(pan_y)
            || !std::isfinite(zoom) || !std::isfinite(grid_step)) {
            if (error_out) error_out->message = "invalid non-finite viewport value";
            return std::nullopt;
        }
        if (zoom <= 0.0f) {
            if (error_out) error_out->message = "invalid viewport zoom: must be > 0";
            return std::nullopt;
        }
        if (zoom > 1000.0f) {
            if (error_out) error_out->message = "invalid viewport zoom: exceeds maximum";
            return std::nullopt;
        }
        if (grid_step <= 0.0f) {
            if (error_out) error_out->message = "invalid viewport grid_step: must be > 0";
            return std::nullopt;
        }
        if (grid_step > 10000.0f) {
            if (error_out) error_out->message = "invalid viewport grid_step: exceeds maximum";
            return std::nullopt;
        }
        bp = bp.with_viewport(pan_x, pan_y, zoom, grid_step);
        return bp;
    } catch (std::exception const& e) {
        if (error_out) error_out->message = e.what();
        return std::nullopt;
    }
}

} // namespace bp2

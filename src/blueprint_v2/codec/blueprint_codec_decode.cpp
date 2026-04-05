#include "blueprint_codec_internal.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace bp2::codec_detail {

namespace {

// ============================================================
// Allowed-field sets (static, constructed once)
// ============================================================

const std::unordered_set<std::string>& allowed_interface_fields() {
    static const std::unordered_set<std::string> s = {
        "name", "domain", "direction", "type", "source_writer"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_node_fields() {
    static const std::unordered_set<std::string> s = {
        "id", "type", "name", "render_hint", "group_id", "expandable", "collapsed",
        "blueprint_path", "position", "width", "height", "params", "string_params",
        "ports", "layout_overrides", "content_type", "content_label", "content_value",
        "content_min", "content_max", "content_unit", "content_state", "content_tripped",
        "has_color", "color_r", "color_g", "color_b", "color_a"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_wire_fields() {
    static const std::unordered_set<std::string> s = {
        "id", "source", "target", "routing_points"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_nested_fields() {
    static const std::unordered_set<std::string> s = {
        "id", "blueprint", "embedded", "position", "definition"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_layout_override_fields() {
    static const std::unordered_set<std::string> s = {
        "port_name", "side", "position"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_port_fields() {
    static const std::unordered_set<std::string> s = {
        "direction", "type"
    };
    return s;
}

// ============================================================
// Port type/domain/direction helpers
// ============================================================

Domain domain_from_port_type(PortType t) {
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

Direction direction_from_port_side(PortSide s) {
    switch (s) {
        case PortSide::Input: return Direction::Input;
        case PortSide::Output: return Direction::Output;
        case PortSide::InOut: return Direction::InOut;
    }
    return Direction::Output;
}

// ============================================================
// Position parsing helper (shared by nodes and nested)
// ============================================================

/// Parse a required position object { "x": num, "y": num }.
/// `context` is used for error messages.
void parse_required_position(nlohmann::json const& obj,
                             std::string const& context,
                             float& out_x, float& out_y) {
    if (!obj.contains("position") || !obj["position"].is_object()) {
        throw std::runtime_error(context + ": missing object field 'position'");
    }
    if (!obj["position"].contains("x") || !obj["position"]["x"].is_number()) {
        throw std::runtime_error(context + ": missing numeric field 'position.x'");
    }
    if (!obj["position"].contains("y") || !obj["position"]["y"].is_number()) {
        throw std::runtime_error(context + ": missing numeric field 'position.y'");
    }
    out_x = parse_finite_float(obj["position"]["x"], "position.x");
    out_y = parse_finite_float(obj["position"]["y"], "position.y");
}

// ============================================================
// Node port decoding helper
// ============================================================

/// Parse port type from a JSON value (integer or string).
/// Throws with appropriate message on unknown type.
PortType parse_port_type(nlohmann::json const& val) {
    if (val.is_number_integer()) {
        const int type_v = val.get<int>();
        if (!is_known_port_type_value(type_v)) {
            throw std::runtime_error("invalid node entry: unknown port type value");
        }
        return static_cast<PortType>(type_v);
    }
    if (val.is_string()) {
        auto pt = port_type_from_name(val.get<std::string>());
        if (!pt) {
            throw std::runtime_error("invalid node entry: unknown port type string");
        }
        return *pt;
    }
    throw std::runtime_error("invalid node entry: port type must be int or string");
}

// ============================================================
// Node iface builder from inputs/outputs
// ============================================================

void build_node_iface(Blueprint::Node& node) {
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

} // namespace

// ============================================================
// decode_interface
// ============================================================

Interface decode_interface(nlohmann::json const& arr,
                           ui::StringInterner& interner) {
    std::vector<PortDescriptor> ports;
    for (auto const& p : arr) {
        if (!p.is_object()) {
            throw std::runtime_error("invalid interface entry: expected object");
        }
        check_allowed_fields(p, allowed_interface_fields(), "interface");
        require_field(p, "name", &nlohmann::json::is_string,
                      "invalid interface entry", "string");
        require_field(p, "domain", &nlohmann::json::is_number_integer,
                      "invalid interface entry", "integer");
        require_field(p, "direction", &nlohmann::json::is_number_integer,
                      "invalid interface entry", "integer");

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

// ============================================================
// decode_nodes
// ============================================================

Blueprint decode_nodes(Blueprint bp,
                       nlohmann::json const& arr,
                       ui::StringInterner& interner,
                       TypeRegistry const& registry) {
    static constexpr auto ctx = "invalid node entry";

    for (auto const& n : arr) {
        if (!n.is_object()) {
            throw std::runtime_error("invalid node entry: expected object");
        }
        require_field(n, "id", &nlohmann::json::is_string, ctx, "string");
        require_field(n, "type", &nlohmann::json::is_string, ctx, "string");
        check_allowed_fields(n, allowed_node_fields(), "node");

        Blueprint::Node node;
        node.id = interner.intern(n["id"].get<std::string>());
        node.type = interner.intern(n["type"].get<std::string>());

        // Optional string fields
        if (auto v = read_optional_string(n, "name", ctx))            node.name = std::move(*v);
        if (auto v = read_optional_string(n, "render_hint", ctx))     node.render_hint = std::move(*v);
        if (auto v = read_optional_string(n, "group_id", ctx))        node.group_id = std::move(*v);
        if (auto v = read_optional_string(n, "blueprint_path", ctx))  node.blueprint_path = std::move(*v);

        // Optional bool fields
        if (auto v = read_optional_bool(n, "expandable", ctx))  node.expandable = *v;
        if (auto v = read_optional_bool(n, "collapsed", ctx))   node.collapsed = *v;

        // Position (optional, defaults to {0,0})
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

        // Optional numeric fields (width, height)
        if (auto v = read_optional_float(n, "width", ctx))  node.width = *v;
        if (auto v = read_optional_float(n, "height", ctx)) node.height = *v;

        // Params
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

        // String params
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

        // Backfill defaults from registry
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

        // Content fields
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
        if (auto v = read_optional_string(n, "content_label", ctx)) node.content_label = std::move(*v);
        if (auto v = read_optional_float(n, "content_value", ctx))  node.content_value = *v;
        if (auto v = read_optional_float(n, "content_min", ctx))    node.content_min = *v;
        if (auto v = read_optional_float(n, "content_max", ctx))    node.content_max = *v;
        if (node.content_min > node.content_max) {
            throw std::runtime_error("invalid node entry: content_min must be <= content_max");
        }
        if (auto v = read_optional_string(n, "content_unit", ctx))  node.content_unit = std::move(*v);
        if (auto v = read_optional_bool(n, "content_state", ctx))   node.content_state = *v;
        if (auto v = read_optional_bool(n, "content_tripped", ctx)) node.content_tripped = *v;

        // Color
        if (auto v = read_optional_bool(n, "has_color", ctx)) {
            node.has_color = *v;
            if (node.has_color) {
                if (auto c = read_optional_float(n, "color_r", ctx)) node.color_r = *c;
                if (auto c = read_optional_float(n, "color_g", ctx)) node.color_g = *c;
                if (auto c = read_optional_float(n, "color_b", ctx)) node.color_b = *c;
                if (auto c = read_optional_float(n, "color_a", ctx)) node.color_a = *c;
            }
        }

        // Layout overrides
        if (n.contains("layout_overrides") && n["layout_overrides"].is_array()) {
            for (auto const& lo : n["layout_overrides"]) {
                if (!lo.is_object()) {
                    throw std::runtime_error("invalid node entry: layout_overrides item must be an object");
                }
                check_allowed_fields(lo, allowed_layout_override_fields(), "layout_overrides");
                require_field(lo, "port_name", &nlohmann::json::is_string,
                              "invalid node entry: layout_overrides", "string");

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

        // Ports
        if (n.contains("ports") && n["ports"].is_object()) {
            for (auto const& [port_name, p] : n["ports"].items()) {
                if (!p.is_object()) {
                    throw std::runtime_error("invalid node entry: port descriptor must be an object");
                }
                check_allowed_fields(p, allowed_port_fields(), "node port");

                auto pid = interner.intern(port_name);

                PortType ptype = PortType::Any;
                if (p.contains("type")) {
                    ptype = parse_port_type(p["type"]);
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

        // Build node.iface from decoded ports
        build_node_iface(node);

        bp = bp.with_node(std::move(node));
    }
    return bp;
}

// ============================================================
// decode_wires
// ============================================================

Blueprint decode_wires(Blueprint bp,
                       nlohmann::json const& arr,
                       ui::StringInterner& interner,
                       PathArena& arena) {
    for (auto const& w : arr) {
        if (!w.is_object()) {
            throw std::runtime_error("invalid wire entry: expected object");
        }
        require_field(w, "id", &nlohmann::json::is_string, "invalid wire entry", "string");
        require_field(w, "source", &nlohmann::json::is_string, "invalid wire entry", "string");
        require_field(w, "target", &nlohmann::json::is_string, "invalid wire entry", "string");
        check_allowed_fields(w, allowed_wire_fields(), "wire");

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

// ============================================================
// decode_nested
// ============================================================

Blueprint decode_nested(Blueprint bp,
                        nlohmann::json const& arr,
                        ui::StringInterner& interner,
                        TypeRegistry const& registry,
                        PathArena& arena) {
    for (auto const& n : arr) {
        if (!n.is_object()) {
            throw std::runtime_error("invalid nested entry: expected object");
        }
        require_field(n, "id", &nlohmann::json::is_string, "invalid nested entry", "string");
        require_field(n, "blueprint", &nlohmann::json::is_string, "invalid nested entry", "string");
        check_allowed_fields(n, allowed_nested_fields(), "nested");

        Blueprint::Nested nested;
        nested.id = interner.intern(n["id"].get<std::string>());
        nested.blueprint_id = interner.intern(n["blueprint"].get<std::string>());
        nested.embedded = n.value("embedded", false);

        // Position (required for nested)
        parse_required_position(n, "invalid nested entry", nested.x, nested.y);

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

} // namespace bp2::codec_detail

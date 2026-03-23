#include "blueprint_codec.h"
#include <nlohmann/json.hpp>
#include <cerrno>
#include <cstdlib>
#include <unordered_map>

namespace bp2 {

namespace {

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
                                 ui::StringInterner const& interner) {
    std::vector<PortDescriptor> sorted = iface.ports();
    std::sort(sorted.begin(), sorted.end(), [&](const PortDescriptor& a, const PortDescriptor& b) {
        std::string_view na = interner.resolve(a.name);
        std::string_view nb = interner.resolve(b.name);
        return na < nb;
    });

    auto arr = nlohmann::json::array();
    for (auto const& port : sorted) {
        nlohmann::json p;
        p["name"] = std::string(interner.resolve(port.name));
        p["domain"] = static_cast<int>(port.domain);
        p["direction"] = static_cast<int>(port.direction);
        arr.push_back(p);
    }
    return arr;
}

nlohmann::json encode_nodes(std::vector<Blueprint::Node> const& nodes,
                             ui::StringInterner const& interner) {
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
        if (!node.params.empty()) {
            nlohmann::json params;
            for (auto const& [k, v] : node.params) {
                params[std::string(interner.resolve(k))] = v;
            }
            n["params"] = params;
        }
        if (!node.string_params.empty()) {
            nlohmann::json sparams;
            for (auto const& [k, v] : node.string_params) {
                sparams[k] = v;
            }
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
    std::vector<Blueprint::Wire const*> sorted;
    sorted.reserve(wires.size());
    for (auto const& wire : wires) sorted.push_back(&wire);
    std::sort(sorted.begin(), sorted.end(), [&](Blueprint::Wire const* a, Blueprint::Wire const* b) {
        std::string_view ida = interner.resolve(a->id);
        std::string_view idb = interner.resolve(b->id);
        return ida < idb;
    });

    auto arr = nlohmann::json::array();
    for (auto const* wire_ptr : sorted) {
        auto const& wire = *wire_ptr;
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
                               PathArena const& arena) {
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
                BlueprintCodec::encode(*nested.inline_def, interner, arena)
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
        PortDescriptor pd;
        pd.name = interner.intern(p["name"].get<std::string>());
        pd.domain = static_cast<Domain>(p["domain"].get<int>());
        pd.direction = static_cast<Direction>(p["direction"].get<int>());
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

        Blueprint::Node node;
        node.id = interner.intern(n["id"].get<std::string>());
        node.type = interner.intern(n["type"].get<std::string>());
        if (n.contains("name") && n["name"].is_string()) {
            node.name = n["name"].get<std::string>();
        }
        if (n.contains("render_hint") && n["render_hint"].is_string()) {
            node.render_hint = n["render_hint"].get<std::string>();
        }
        if (n.contains("group_id") && n["group_id"].is_string()) {
            node.group_id = n["group_id"].get<std::string>();
        }
        if (n.contains("expandable") && n["expandable"].is_boolean()) {
            node.expandable = n["expandable"].get<bool>();
        }
        if (n.contains("collapsed") && n["collapsed"].is_boolean()) {
            node.collapsed = n["collapsed"].get<bool>();
        }
        if (n.contains("blueprint_path") && n["blueprint_path"].is_string()) {
            node.blueprint_path = n["blueprint_path"].get<std::string>();
        }
        if (!n.contains("position") || !n["position"].is_object()) {
            throw std::runtime_error("invalid node entry: missing object field 'position'");
        }
        if (!n["position"].contains("x") || !n["position"]["x"].is_number()) {
            throw std::runtime_error("invalid node entry: missing numeric field 'position.x'");
        }
        if (!n["position"].contains("y") || !n["position"]["y"].is_number()) {
            throw std::runtime_error("invalid node entry: missing numeric field 'position.y'");
        }
        node.x = n["position"]["x"].get<float>();
        node.y = n["position"]["y"].get<float>();
        if (n.contains("width") && n["width"].is_number()) {
            node.width = n["width"].get<float>();
        }
        if (n.contains("height") && n["height"].is_number()) {
            node.height = n["height"].get<float>();
        }
        if (n.contains("params") && n["params"].is_object()) {
            for (auto& [key, val] : n["params"].items()) {
                if (val.is_number()) {
                    node.params[interner.intern(key)] = val.get<float>();
                    continue;
                }
                if (val.is_string()) {
                    const std::string s = val.get<std::string>();
                    char* end = nullptr;
                    errno = 0;
                    const float parsed = std::strtof(s.c_str(), &end);
                    const bool parsed_ok = (end != s.c_str() && *end == '\0' && errno != ERANGE);
                    if (parsed_ok) {
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
                if (val.is_string()) {
                    node.string_params[key] = val.get<std::string>();
                }
            }
        }

        if (auto* entry = registry.find(node.type)) {
            for (const auto& [k, v] : entry->param_defaults) {
                const auto key_iid = interner.intern(k);
                if (node.params.find(key_iid) != node.params.end()) {
                    continue;
                }
                if (node.string_params.find(k) != node.string_params.end()) {
                    continue;
                }
                char* end = nullptr;
                errno = 0;
                const float parsed = std::strtof(v.c_str(), &end);
                const bool parsed_ok = (end != v.c_str() && *end == '\0' && errno != ERANGE);
                if (parsed_ok) {
                    node.params[key_iid] = parsed;
                } else {
                    node.string_params[k] = v;
                }
            }
        }

        if (n.contains("content_type") && n["content_type"].is_number_integer()) {
            int ct = n["content_type"].get<int>();
            if (ct >= 0 && ct <= static_cast<int>(NodeContentType::Slider)) {
                node.content_type = static_cast<NodeContentType>(ct);
            }
        }
        if (n.contains("content_label") && n["content_label"].is_string()) {
            node.content_label = n["content_label"].get<std::string>();
        }
        if (n.contains("content_value") && n["content_value"].is_number()) {
            node.content_value = n["content_value"].get<float>();
        }
        if (n.contains("content_min") && n["content_min"].is_number()) {
            node.content_min = n["content_min"].get<float>();
        }
        if (n.contains("content_max") && n["content_max"].is_number()) {
            node.content_max = n["content_max"].get<float>();
        }
        if (n.contains("content_unit") && n["content_unit"].is_string()) {
            node.content_unit = n["content_unit"].get<std::string>();
        }
        if (n.contains("content_state") && n["content_state"].is_boolean()) {
            node.content_state = n["content_state"].get<bool>();
        }
        if (n.contains("content_tripped") && n["content_tripped"].is_boolean()) {
            node.content_tripped = n["content_tripped"].get<bool>();
        }

        if (n.contains("has_color") && n["has_color"].is_boolean()) {
            node.has_color = n["has_color"].get<bool>();
            if (node.has_color) {
                node.color_r = n.value("color_r", node.color_r);
                node.color_g = n.value("color_g", node.color_g);
                node.color_b = n.value("color_b", node.color_b);
                node.color_a = n.value("color_a", node.color_a);
            }
        }

        if (n.contains("layout_overrides") && n["layout_overrides"].is_array()) {
            for (auto const& lo : n["layout_overrides"]) {
                if (!lo.is_object()) continue;
                if (!lo.contains("port_name") || !lo["port_name"].is_string()) continue;
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
                }
                node.layout_overrides.push_back(std::move(ov));
            }
        }

        if (n.contains("ports") && n["ports"].is_object()) {
            for (auto const& [port_name, p] : n["ports"].items()) {
                if (!p.is_object()) continue;
                auto pid = interner.intern(port_name);

                PortType ptype = PortType::Any;
                if (p.contains("type")) {
                    if (p["type"].is_number_integer()) {
                        ptype = static_cast<PortType>(p["type"].get<int>());
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

                std::string dir = p.value("direction", std::string("Out"));
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
                wire.routing_points.emplace_back(rp[0].get<float>(), rp[1].get<float>());
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
        nested.x = n["position"]["x"].get<float>();
        nested.y = n["position"]["y"].get<float>();
        if (nested.embedded && n.contains("definition")) {
            auto inner = BlueprintCodec::decode(n["definition"].dump(), interner, arena, registry);
            if (inner) {
                nested.inline_def = std::make_unique<Blueprint>(std::move(*inner));
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
                                    PathArena const& arena) {
    nlohmann::json j;
    j["version"] = "3.0";
    j["id"] = std::string(interner.resolve(bp.id()));
    j["display_name"] = bp.display_name();
    j["interface"] = encode_interface(bp.iface(), interner);
    j["nodes"] = encode_nodes(bp.nodes(), interner);
    j["wires"] = encode_wires(bp.wires(), interner, arena);
    j["nested"] = encode_nested(bp.nested(), interner, arena);
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

        Blueprint bp;
        bp = bp.with_id(interner.intern(j["id"].get<std::string>()));
        bp = bp.with_display_name(j["display_name"].get<std::string>());
        if (j.contains("name") && j["name"].is_string()) {
            bp = bp.with_name(j["name"].get<std::string>());
        }
        bp = bp.with_interface(decode_interface(j["interface"], interner));
        bp = decode_nodes(bp, j["nodes"], interner, registry);
        bp = decode_wires(bp, j["wires"], interner, arena);
        bp = decode_nested(bp, j["nested"], interner, registry, arena);
        bp = bp.with_viewport(
            j.value("pan_x", 0.0f),
            j.value("pan_y", 0.0f),
            j.value("zoom", 1.0f),
            j.value("grid_step", 16.0f));
        return bp;
    } catch (std::exception const& e) {
        if (error_out) error_out->message = e.what();
        return std::nullopt;
    }
}

} // namespace bp2

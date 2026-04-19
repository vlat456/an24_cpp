#include "blueprint_codec_internal.h"

#include <algorithm>

namespace bp2::codec_detail {

namespace {

std::string encode_direction(Direction direction) {
    switch (direction) {
        case Direction::Input: return "In";
        case Direction::Output: return "Out";
        case Direction::InOut: return "InOut";
    }
    return "Out";
}

std::string encode_bridge_side(Blueprint::Node::BridgePortSide side) {
    return side == Blueprint::Node::BridgePortSide::Input ? "input" : "output";
}

std::string encode_node_kind(Blueprint::Node const& node) {
    if (node.is_component()) return "component";
    if (node.is_blueprint_instance()) return "blueprint_instance";
    return "bridge_port";
}

nlohmann::json encode_node_source(const Blueprint::Node& node,
                                  ui::StringInterner const& interner,
                                  PathArena const& arena,
                                  ::TypeRegistry const* parser_registry) {
    const auto& node_source = node.blueprint_instance().source;

    nlohmann::json encoded_source;
    if (node_source.is_embedded()) {
        encoded_source["mode"] = "embedded";
        const Blueprint* inline_bp = node_source.inline_def();
        if (!inline_bp) {
            throw std::logic_error("encode_node_source: embedded source missing inline blueprint");
        }
        encoded_source["blueprint"] = nlohmann::json::parse(
            BlueprintCodec::encode(*inline_bp, interner, arena, parser_registry));
        return encoded_source;
    }

    encoded_source["mode"] = "reference";
    encoded_source["blueprint_id"] = std::string(interner.resolve(node_source.blueprint_id()));
    return encoded_source;
}

} // namespace

nlohmann::json encode_interface(Interface const& iface,
                                ui::StringInterner const& interner,
                                ComponentSpec const* type_def) {
    std::vector<PortDescriptor> sorted = iface.ports();
    std::sort(sorted.begin(), sorted.end(), [&](const PortDescriptor& a, const PortDescriptor& b) {
        return interner.resolve(a.name) < interner.resolve(b.name);
    });

    auto arr = nlohmann::json::array();
    for (auto const& port : sorted) {
        nlohmann::json p;
        const std::string name = std::string(interner.resolve(port.name));
        p["id"] = name;
        p["direction"] = encode_direction(port.direction);
        p["port_type"] = port_type_to_string(port.port_type);

        bool serialized_source_writer = false;
        if (type_def) {
            const auto& ports = spec_ports(*type_def);
            auto it = ports.find(name);
            if (it != ports.end()) {
                serialized_source_writer = it->second.source_writer;
            }
        }
        if (serialized_source_writer) {
            p["source_writer"] = true;
        }
        arr.push_back(std::move(p));
    }
    return arr;
}

nlohmann::json encode_nodes(std::vector<Blueprint::Node> const& nodes,
                            ui::StringInterner const& interner,
                            PathArena const& arena,
                            ::TypeRegistry const* parser_registry) {
    (void)parser_registry;

    std::vector<Blueprint::Node const*> sorted;
    sorted.reserve(nodes.size());
    for (auto const& node : nodes) {
        sorted.push_back(&node);
    }
    std::sort(sorted.begin(), sorted.end(), [&](Blueprint::Node const* a, Blueprint::Node const* b) {
        return interner.resolve(a->semantic.id) < interner.resolve(b->semantic.id);
    });

    auto arr = nlohmann::json::array();
    for (auto const* node_ptr : sorted) {
        auto const& node = *node_ptr;
        nlohmann::json n;
        n["id"] = std::string(interner.resolve(node.semantic.id));
        n["kind"] = encode_node_kind(node);
        if (!node.view.name.empty()) {
            n["label"] = node.view.name;
        }

        if (node.is_component()) {
            n["component"] = std::string(interner.resolve(node.semantic.type));
        } else if (node.is_blueprint_instance()) {
            n["source"] = encode_node_source(node, interner, arena, parser_registry);
            if (!node.layout.collapsed) {
                n["collapsed"] = false;
            }
        } else {
            n["exposed_port"] = std::string(interner.resolve(node.bridge_port().exposed_port));
            n["side"] = encode_bridge_side(node.bridge_port().side);
            n["port_type"] = port_type_to_string(node.bridge_port().port_type);
        }

        const ComponentSpec* type_def = parser_registry
            ? parser_registry->get(std::string(interner.resolve(node.semantic.type)))
            : nullptr;

        nlohmann::json params = nlohmann::json::object();
        for (auto const& [k, v] : node.semantic.params) {
            const std::string key = std::string(interner.resolve(k));
            if (type_def) {
                const auto& type_params = spec_params(*type_def);
                auto schema_it = type_params.find(key);
                if (schema_it != type_params.end()) {
                    switch (schema_it->second.type) {
                        case ParamSchemaType::Bool:
                            params[key] = (v != 0.0f);
                            continue;
                        case ParamSchemaType::Int:
                            params[key] = static_cast<int>(v);
                            continue;
                        case ParamSchemaType::Float:
                        case ParamSchemaType::String:
                            break;
                    }
                }
            }
            params[key] = v;
        }
        for (auto const& [k, v] : node.semantic.string_params) {
            if (type_def) {
                const auto& type_params = spec_params(*type_def);
                auto schema_it = type_params.find(k);
                if (schema_it != type_params.end()) {
                    if (schema_it->second.type == ParamSchemaType::Bool) {
                        params[k] = (v == "true" || v == "1");
                        continue;
                    }
                }
            }
            params[k] = v;
        }
        if (!params.empty()) {
            if (!node.is_component()) {
                throw std::logic_error("encode_nodes: non-component node has params");
            }
            n["params"] = std::move(params);
        }

        nlohmann::json layout;
        layout["x"] = node.layout.x;
        layout["y"] = node.layout.y;
        if (node.layout.width.has_value()) {
            layout["width"] = *node.layout.width;
        }
        if (node.layout.height.has_value()) {
            layout["height"] = *node.layout.height;
        }
        if (node.layout.manual_size) {
            layout["manual_size"] = true;
        }
        if (!node.layout.layout_overrides.empty()) {
            nlohmann::json overrides = nlohmann::json::array();
            for (auto const& override : node.layout.layout_overrides) {
                nlohmann::json encoded;
                encoded["port_id"] = override.port_name;
                if (override.side.has_value()) {
                    encoded["side"] = *override.side;
                }
                if (override.position.has_value()) {
                    encoded["position"] = *override.position;
                }
                overrides.push_back(std::move(encoded));
            }
            layout["port_overrides"] = std::move(overrides);
        }
        n["layout"] = std::move(layout);

        arr.push_back(std::move(n));
    }

    return arr;
}

nlohmann::json encode_wires(std::vector<Blueprint::Wire> const& wires,
                            ui::StringInterner const& interner) {
    auto arr = nlohmann::json::array();
    for (auto const& wire : wires) {
        nlohmann::json w;
        w["id"] = std::string(interner.resolve(wire.id));

        w["from"] = {
            {"node", std::string(interner.resolve(wire.source.node))},
            {"port", std::string(interner.resolve(wire.source.port))}
        };
        w["to"] = {
            {"node", std::string(interner.resolve(wire.target.node))},
            {"port", std::string(interner.resolve(wire.target.port))}
        };

        if (!wire.routing_points.empty()) {
            nlohmann::json routing = nlohmann::json::array();
            for (auto const& [x, y] : wire.routing_points) {
                routing.push_back({x, y});
            }
            w["routing"] = std::move(routing);
        }

        arr.push_back(std::move(w));
    }
    return arr;
}

} // namespace bp2::codec_detail

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

std::string encode_node_kind(Blueprint::Node::Kind kind) {
    switch (kind) {
        case Blueprint::Node::Kind::Component: return "component";
        case Blueprint::Node::Kind::BlueprintInstance: return "blueprint_instance";
    }
    return "component";
}

nlohmann::json encode_node_source(const Blueprint::Node& node,
                                  ui::StringInterner const& interner,
                                  PathArena const& arena,
                                  ::TypeRegistry const* parser_registry) {
    if (!node.source.has_value()) {
        throw std::logic_error("encode_node_source: blueprint_instance node missing source");
    }

    nlohmann::json source;
    if (node.source->is_embedded()) {
        source["mode"] = "embedded";
        const Blueprint* inline_bp = node.source->inline_def();
        if (!inline_bp) {
            throw std::logic_error("encode_node_source: embedded source missing inline blueprint");
        }
        source["blueprint"] = nlohmann::json::parse(
            BlueprintCodec::encode(*inline_bp, interner, arena, parser_registry));
        return source;
    }

    source["mode"] = "reference";
    source["blueprint_id"] = std::string(interner.resolve(node.source->blueprint_id()));
    return source;
}

} // namespace

nlohmann::json encode_interface(Interface const& iface,
                                ui::StringInterner const& interner,
                                TypeDefinition const* type_def) {
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
            auto it = type_def->ports.find(name);
            if (it != type_def->ports.end()) {
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
        n["kind"] = encode_node_kind(node.kind);
        if (!node.view.name.empty()) {
            n["label"] = node.view.name;
        }

        if (node.is_component()) {
            n["component"] = std::string(interner.resolve(node.semantic.type));
        } else {
            n["source"] = encode_node_source(node, interner, arena, parser_registry);
            if (!node.layout.collapsed) {
                n["collapsed"] = false;
            }
        }

        nlohmann::json params = nlohmann::json::object();
        for (auto const& [k, v] : node.semantic.params) {
            params[std::string(interner.resolve(k))] = v;
        }
        for (auto const& [k, v] : node.semantic.string_params) {
            params[k] = v;
        }
        if (!params.empty()) {
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

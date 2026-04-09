#include "blueprint_codec_internal.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace bp2::codec_detail {

namespace {

bool is_default_node_content(const Blueprint::Node& node) {
    return node.view.content_type == NodeContentType::None
        && node.view.content_label.empty()
        && node.view.content_value == 0.0f
        && node.view.content_min == 0.0f
        && node.view.content_max == 1.0f
        && node.view.content_unit.empty()
        && !node.view.content_state
        && !node.view.content_tripped;
}

nlohmann::json encode_node_ports(const Blueprint::Node& node,
                                 ui::StringInterner const& interner) {
    nlohmann::json ports = nlohmann::json::object();

    for (auto const& pdesc : node.semantic.iface.ports()) {
        nlohmann::json p;
        p["direction"] = (pdesc.direction == Direction::Input)
            ? "In"
            : (pdesc.direction == Direction::Output ? "Out" : "InOut");
        p["type"] = static_cast<int>(pdesc.port_type);
        ports[std::string(interner.resolve(pdesc.name))] = std::move(p);
    }

    return ports;
}

} // namespace

nlohmann::json encode_interface(Interface const& iface,
                                ui::StringInterner const& interner,
                                TypeDefinition const* type_def) {
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
        bool serialized_source_writer = false;
        if (type_def) {
            auto it = type_def->ports.find(name);
            if (it != type_def->ports.end()) {
                serialized_source_writer = it->second.source_writer;
            }
        }
        p["type"] = port_type_to_string(port.port_type);
        p["source_writer"] = serialized_source_writer;
        arr.push_back(p);
    }
    return arr;
}

nlohmann::json encode_nodes(std::vector<Blueprint::Node> const& nodes,
                           std::vector<Blueprint::Nested> const& nested_vec,
                           ui::StringInterner const& interner,
                           ::TypeRegistry const* parser_registry) {
    std::vector<Blueprint::Node const*> sorted;
    sorted.reserve(nodes.size());
    for (auto const& node : nodes) {
        sorted.push_back(&node);
    }
    std::sort(sorted.begin(), sorted.end(), [&](Blueprint::Node const* a, Blueprint::Node const* b) {
        std::string_view ida = interner.resolve(a->semantic.id);
        std::string_view idb = interner.resolve(b->semantic.id);
        if (ida == idb) {
            return interner.resolve(a->semantic.type) < interner.resolve(b->semantic.type);
        }
        return ida < idb;
    });

    auto arr = nlohmann::json::array();
    for (auto const* node_ptr : sorted) {
        auto const& node = *node_ptr;
        const bool is_hosted_nested = std::any_of(
            nested_vec.begin(), nested_vec.end(),
            [&](const Blueprint::Nested& nested) { return nested.id == node.semantic.id; });
        nlohmann::json n;
        n["id"] = std::string(interner.resolve(node.semantic.id));
        n["type"] = std::string(interner.resolve(node.semantic.type));
        if (!node.view.name.empty()) n["name"] = node.view.name;
        if (!node.view.render_hint.empty()) n["render_hint"] = node.view.render_hint;
        if (!node.structure.owner_scope.empty()) n["group_id"] = node.structure.owner_scope;
        if (node.view.expandable) n["expandable"] = true;
        if (!node.layout.collapsed) n["collapsed"] = false;
        if (!node.view.blueprint_path.empty() && (!node.view.expandable || !is_hosted_nested)) {
            // Persist blueprint_path for external-reference style nodes.
            // Hosted nested instances do not use this field as authority.
            n["blueprint_path"] = node.view.blueprint_path;
        }
        n["position"] = {{"x", node.layout.x}, {"y", node.layout.y}};
        if (node.layout.width.has_value()) n["width"] = *node.layout.width;
        if (node.layout.height.has_value()) n["height"] = *node.layout.height;

        nlohmann::json params = nlohmann::json::object();
        nlohmann::json sparams = nlohmann::json::object();
        std::unordered_set<std::string> descriptor_keys;
        const TypeDefinition* def = nullptr;
        if (parser_registry) {
            def = parser_registry->get(std::string(interner.resolve(node.semantic.type)));
        }

        if (def) {
            for (const auto& [key, schema] : def->param_schema) {
                descriptor_keys.insert(key);
                ui::InternedId key_iid = interner.lookup(key);
                const auto pit = key_iid.empty() ? node.semantic.params.end() : node.semantic.params.find(key_iid);
                const auto sit = node.semantic.string_params.find(key);

                switch (schema.type) {
                    case ParamSchemaType::Float:
                    case ParamSchemaType::Int: {
                        if (pit != node.semantic.params.end()) {
                            params[key] = pit->second;
                        } else if (sit != node.semantic.string_params.end()) {
                            float parsed = 0.0f;
                            if (parse_number_string(sit->second, parsed)) {
                                params[key] = parsed;
                            }
                        }
                        break;
                    }
                    case ParamSchemaType::Bool: {
                        if (sit != node.semantic.string_params.end()) {
                            std::string normalized;
                            if (parse_bool_string(sit->second, normalized)) {
                                params[key] = (normalized == "true");
                            }
                        } else if (pit != node.semantic.params.end()) {
                            params[key] = (pit->second != 0.0f);
                        }
                        break;
                    }
                    case ParamSchemaType::String: {
                        if (sit != node.semantic.string_params.end()) {
                            params[key] = sit->second;
                        } else if (pit != node.semantic.params.end()) {
                            params[key] = float_to_string(pit->second);
                        }
                        break;
                    }
                }
            }
        }

        for (auto const& [k, v] : node.semantic.params) {
            std::string key = std::string(interner.resolve(k));
            if (descriptor_keys.find(key) != descriptor_keys.end()) {
                continue;
            }
            params[key] = v;
        }
        for (auto const& [k, v] : node.semantic.string_params) {
            if (descriptor_keys.find(k) != descriptor_keys.end()) {
                continue;
            }
            sparams[k] = v;
        }

        if (!params.empty()) n["params"] = params;
        if (!sparams.empty()) n["string_params"] = sparams;
        if (!node.semantic.iface.empty()) n["ports"] = encode_node_ports(node, interner);

        if (!node.layout.layout_overrides.empty()) {
            nlohmann::json los = nlohmann::json::array();
            for (auto const& lo : node.layout.layout_overrides) {
                nlohmann::json jlo;
                jlo["port_name"] = lo.port_name;
                if (lo.side.has_value()) jlo["side"] = *lo.side;
                if (lo.position.has_value()) jlo["position"] = *lo.position;
                los.push_back(std::move(jlo));
            }
            n["layout_overrides"] = std::move(los);
        }

        if (!is_default_node_content(node)) {
            n["content_type"] = static_cast<int>(node.view.content_type);
            n["content_label"] = node.view.content_label;
            n["content_value"] = node.view.content_value;
            n["content_min"] = node.view.content_min;
            n["content_max"] = node.view.content_max;
            n["content_unit"] = node.view.content_unit;
            n["content_state"] = node.view.content_state;
            n["content_tripped"] = node.view.content_tripped;
        }

        if (node.view.has_color) {
            n["has_color"] = true;
            n["color_r"] = node.view.color_r;
            n["color_g"] = node.view.color_g;
            n["color_b"] = node.view.color_b;
            n["color_a"] = node.view.color_a;
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
                             ::TypeRegistry const* parser_registry) {
    std::vector<Blueprint::Nested const*> sorted;
    sorted.reserve(nested_vec.size());
    for (auto const& nested : nested_vec) {
        sorted.push_back(&nested);
    }
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
        n["blueprint"] = std::string(interner.resolve(nested.blueprint_id()));
        n["embedded"] = nested.is_embedded();
        n["position"] = {{"x", nested.x}, {"y", nested.y}};
        if (auto* def = nested.inline_def()) {
            const std::string encoded = BlueprintCodec::encode(
                *def,
                interner,
                arena,
                parser_registry);
            n["definition"] = nlohmann::json::parse(
                encoded
            );
        }
        arr.push_back(n);
    }
    return arr;
}

} // namespace bp2::codec_detail

#include "blueprint_codec_internal.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace bp2::codec_detail {

namespace {

bool is_default_node_content(const Blueprint::Node& node) {
    return node.content_type == NodeContentType::None
        && node.content_label.empty()
        && node.content_value == 0.0f
        && node.content_min == 0.0f
        && node.content_max == 1.0f
        && node.content_unit.empty()
        && !node.content_state
        && !node.content_tripped;
}

nlohmann::json encode_node_ports(const Blueprint::Node& node,
                                 ui::StringInterner const& interner) {
    nlohmann::json ports = nlohmann::json::object();

    std::unordered_map<ui::InternedId, Direction> dirs;
    std::unordered_map<ui::InternedId, PortType> types;

    for (auto const& p : node.inputs) {
        auto it = dirs.find(p.name);
        if (it == dirs.end()) {
            dirs[p.name] = Direction::Input;
            types[p.name] = p.type;
        } else if (it->second == Direction::Output) {
            it->second = Direction::InOut;
        }
    }
    for (auto const& p : node.outputs) {
        auto it = dirs.find(p.name);
        if (it == dirs.end()) {
            dirs[p.name] = Direction::Output;
            types[p.name] = p.type;
        } else if (it->second == Direction::Input) {
            it->second = Direction::InOut;
        }
    }

    for (auto const& [name, dir] : dirs) {
        nlohmann::json p;
        p["direction"] = (dir == Direction::Input) ? "In" : (dir == Direction::Output ? "Out" : "InOut");
        p["type"] = static_cast<int>(types[name]);
        ports[std::string(interner.resolve(name))] = std::move(p);
    }

    return ports;
}

} // namespace

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
    for (auto const& node : nodes) {
        sorted.push_back(&node);
    }
    std::sort(sorted.begin(), sorted.end(), [&](Blueprint::Node const* a, Blueprint::Node const* b) {
        std::string_view ida = interner.resolve(a->id);
        std::string_view idb = interner.resolve(b->id);
        if (ida == idb) {
            return interner.resolve(a->type) < interner.resolve(b->type);
        }
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

        if (!params.empty()) n["params"] = params;
        if (!sparams.empty()) n["string_params"] = sparams;
        if (!node.inputs.empty() || !node.outputs.empty()) n["ports"] = encode_node_ports(node, interner);

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

} // namespace bp2::codec_detail

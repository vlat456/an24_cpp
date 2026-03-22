#include "blueprint_codec.h"
#include <nlohmann/json.hpp>

namespace bp2 {

namespace {

nlohmann::json encode_interface(Interface const& iface,
                                 ui::StringInterner const& interner) {
    auto arr = nlohmann::json::array();
    for (auto const& port : iface.ports()) {
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
    auto arr = nlohmann::json::array();
    for (auto const& node : nodes) {
        nlohmann::json n;
        n["id"] = std::string(interner.resolve(node.id));
        n["type"] = std::string(interner.resolve(node.type));
        n["position"] = {{"x", node.x}, {"y", node.y}};
        if (!node.params.empty()) {
            nlohmann::json params;
            for (auto const& [k, v] : node.params) {
                params[std::string(interner.resolve(k))] = v;
            }
            n["params"] = params;
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
        arr.push_back(w);
    }
    return arr;
}

nlohmann::json encode_nested(std::vector<Blueprint::Nested> const& nested_vec,
                              ui::StringInterner const& interner,
                              PathArena const& arena) {
    auto arr = nlohmann::json::array();
    for (auto const& nested : nested_vec) {
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

Blueprint decode_nodes(Blueprint bp, nlohmann::json const& arr,
                       ui::StringInterner& interner) {
    for (auto const& n : arr) {
        Blueprint::Node node;
        node.id = interner.intern(n["id"].get<std::string>());
        node.type = interner.intern(n["type"].get<std::string>());
        if (n.contains("position")) {
            node.x = n["position"].value("x", 0.0f);
            node.y = n["position"].value("y", 0.0f);
        }
        if (n.contains("params") && n["params"].is_object()) {
            for (auto& [key, val] : n["params"].items()) {
                node.params[interner.intern(key)] = val.get<float>();
            }
        }
        bp = bp.with_node(std::move(node));
    }
    return bp;
}

Blueprint decode_wires(Blueprint bp, nlohmann::json const& arr,
                        ui::StringInterner& interner,
                        PathArena& arena) {
    for (auto const& w : arr) {
        Blueprint::Wire wire;
        wire.id = interner.intern(w["id"].get<std::string>());
        auto src = arena.parse(w["source"].get<std::string>());
        auto tgt = arena.parse(w["target"].get<std::string>());
        if (src) wire.source = *src;
        if (tgt) wire.target = *tgt;
        bp = bp.with_wire(std::move(wire));
    }
    return bp;
}

Blueprint decode_nested(Blueprint bp, nlohmann::json const& arr,
                         ui::StringInterner& interner,
                         TypeRegistry const& registry,
                         PathArena& arena) {
    for (auto const& n : arr) {
        Blueprint::Nested nested;
        nested.id = interner.intern(n["id"].get<std::string>());
        if (n.contains("blueprint") && n["blueprint"].is_string()) {
            nested.blueprint_id = interner.intern(n["blueprint"].get<std::string>());
        }
        nested.embedded = n.value("embedded", false);
        if (n.contains("position")) {
            nested.x = n["position"].value("x", 0.0f);
            nested.y = n["position"].value("y", 0.0f);
        }
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
        Blueprint bp;
        if (j.contains("id") && j["id"].is_string()) {
            bp = bp.with_id(interner.intern(j["id"].get<std::string>()));
        }
        if (j.contains("display_name") && j["display_name"].is_string()) {
            bp = bp.with_display_name(j["display_name"].get<std::string>());
        }
        if (j.contains("interface") && j["interface"].is_array()) {
            bp = bp.with_interface(decode_interface(j["interface"], interner));
        }
        if (j.contains("nodes") && j["nodes"].is_array()) {
            bp = decode_nodes(bp, j["nodes"], interner);
        }
        if (j.contains("wires") && j["wires"].is_array()) {
            bp = decode_wires(bp, j["wires"], interner, arena);
        }
        if (j.contains("nested") && j["nested"].is_array()) {
            bp = decode_nested(bp, j["nested"], interner, registry, arena);
        }
        return bp;
    } catch (std::exception const& e) {
        if (error_out) error_out->message = e.what();
        return std::nullopt;
    }
}

} // namespace bp2
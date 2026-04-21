#include "blueprint_codec_internal.h"
#include "blueprint_v2/interface/type_definition_interface.h"
#include "blueprint_v2/validation/signal_typing.h"
#include "blueprint_v2/validation/path_resolver.h"

#include <algorithm>
#include <unordered_set>

namespace bp2::codec_detail {

namespace {

const std::unordered_set<std::string>& allowed_interface_fields() {
    static const std::unordered_set<std::string> s = {
        "id", "direction", "port_type", "source_writer"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_component_node_fields() {
    static const std::unordered_set<std::string> s = {
        "id", "kind", "label", "component", "params", "layout"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_blueprint_instance_node_fields() {
    static const std::unordered_set<std::string> s = {
        "id", "kind", "label", "source", "collapsed", "layout"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_bridge_port_node_fields() {
    static const std::unordered_set<std::string> s = {
        "id", "kind", "label", "exposed_port", "direction", "port_type", "layout"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_layout_fields() {
    static const std::unordered_set<std::string> s = {
        "x", "y", "width", "height", "manual_size", "port_overrides"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_layout_override_fields() {
    static const std::unordered_set<std::string> s = {
        "port_id", "side", "position"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_source_fields_embedded() {
    static const std::unordered_set<std::string> s = {
        "mode", "blueprint"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_source_fields_reference() {
    static const std::unordered_set<std::string> s = {
        "mode", "blueprint_id"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_wire_fields() {
    static const std::unordered_set<std::string> s = {
        "id", "from", "to", "routing"
    };
    return s;
}

const std::unordered_set<std::string>& allowed_endpoint_fields() {
    static const std::unordered_set<std::string> s = {
        "node", "port"
    };
    return s;
}

Direction decode_direction(std::string const& dir) {
    if (dir == "In") return Direction::Input;
    if (dir == "Out") return Direction::Output;
    if (dir == "InOut") return Direction::InOut;
    throw std::runtime_error("invalid interface entry: unknown direction token");
}

enum class DecodedNodeKind {
    Component,
    BlueprintInstance,
    BridgePort,
};

DecodedNodeKind decode_node_kind(std::string const& kind) {
    if (kind == "component") return DecodedNodeKind::Component;
    if (kind == "blueprint_instance") return DecodedNodeKind::BlueprintInstance;
    if (kind == "bridge_port") return DecodedNodeKind::BridgePort;
    throw std::runtime_error("invalid node entry: unknown node kind");
}

bp2::BridgeDirection decode_bridge_direction(std::string const& direction) {
    if (direction == "input") return bp2::BridgeDirection::Input;
    if (direction == "output") return bp2::BridgeDirection::Output;
    throw std::runtime_error("invalid node entry: unknown bridge direction");
}

Blueprint::Node::BlueprintSource decode_node_source(nlohmann::json const& source,
                                                    ui::StringInterner& interner,
                                                    ::ComponentRegistry const& parser_registry,
                                                    PathArena& arena) {
    static constexpr auto ctx = "invalid node entry: source";
    if (!source.is_object()) {
        throw std::runtime_error(std::string(ctx) + " must be object");
    }
    require_field(source, "mode", &nlohmann::json::is_string, ctx, "string");
    const std::string mode = source["mode"].get<std::string>();

    if (mode == "embedded") {
        check_allowed_fields(source, allowed_source_fields_embedded(), "source");
        require_field(source, "blueprint", &nlohmann::json::is_object, ctx, "object");
        DecodeError inner_err;
        auto inner = BlueprintCodec::decode(source["blueprint"].dump(), interner, arena, parser_registry, &inner_err);
        if (!inner.has_value()) {
            throw std::runtime_error("invalid node entry: embedded blueprint decode failed: " + inner_err.message);
        }
        return Blueprint::Node::BlueprintSource::make_embedded(std::make_unique<Blueprint>(std::move(*inner)));
    }

    if (mode == "reference") {
        check_allowed_fields(source, allowed_source_fields_reference(), "source");
        require_field(source, "blueprint_id", &nlohmann::json::is_string, ctx, "string");
        const auto bp_id = interner.intern(source["blueprint_id"].get<std::string>());
        const auto* def = parser_registry.get(std::string(interner.resolve(bp_id)));
        if (!def) {
            throw std::runtime_error("invalid node entry: unknown referenced blueprint_id");
        }
        return Blueprint::Node::BlueprintSource::make_reference(
            bp_id);
    }

    throw std::runtime_error("invalid node entry: unknown source.mode");
}

WireEndpoint decode_endpoint(nlohmann::json const& endpoint,
                     ui::StringInterner& interner) {
    if (!endpoint.is_object()) {
        throw std::runtime_error("invalid wire entry: endpoint must be object");
    }
    check_allowed_fields(endpoint, allowed_endpoint_fields(), "wire endpoint");
    require_field(endpoint, "node", &nlohmann::json::is_string, "invalid wire entry: endpoint", "string");
    require_field(endpoint, "port", &nlohmann::json::is_string, "invalid wire entry: endpoint", "string");

    WireEndpoint ep;
    ep.node = interner.intern(endpoint["node"].get<std::string>());
    ep.port = interner.intern(endpoint["port"].get<std::string>());
    return ep;
}

} // namespace

Interface decode_interface(nlohmann::json const& arr,
                           ui::StringInterner& interner) {
    std::vector<PortDescriptor> ports;
    std::unordered_set<std::string> seen_ids;
    for (auto const& p : arr) {
        if (!p.is_object()) {
            throw std::runtime_error("invalid interface entry: expected object");
        }
        check_allowed_fields(p, allowed_interface_fields(), "interface");
        require_field(p, "id", &nlohmann::json::is_string, "invalid interface entry", "string");
        require_field(p, "direction", &nlohmann::json::is_string, "invalid interface entry", "string");
        require_field(p, "port_type", &nlohmann::json::is_string, "invalid interface entry", "string");

        const std::string id_str = p["id"].get<std::string>();
        if (!seen_ids.insert(id_str).second) {
            throw std::runtime_error("invalid interface entry: duplicate port id '" + id_str + "'");
        }

        PortDescriptor pd;
        pd.name = interner.intern(id_str);
        pd.direction = decode_direction(p["direction"].get<std::string>());
        auto port_type = port_type_from_name(p["port_type"].get<std::string>());
        if (!port_type.has_value()) {
            throw std::runtime_error("invalid interface entry: unknown port_type");
        }
        pd.port_type = *port_type;
        pd.domain = domain_for_port_type(pd.port_type);
        ports.push_back(std::move(pd));
    }
    return Interface(std::move(ports));
}

Blueprint decode_nodes(Blueprint bp,
                       nlohmann::json const& arr,
                       ui::StringInterner& interner,
                       ::ComponentRegistry const& parser_registry) {
    static constexpr auto ctx = "invalid node entry";

    PathArena local_arena(interner);
    for (auto const& n : arr) {
        if (!n.is_object()) {
            throw std::runtime_error("invalid node entry: expected object");
        }
        require_field(n, "id", &nlohmann::json::is_string, ctx, "string");
        require_field(n, "kind", &nlohmann::json::is_string, ctx, "string");
        require_field(n, "layout", &nlohmann::json::is_object, ctx, "object");

        Blueprint::Node node;
        node.semantic.id = interner.intern(n["id"].get<std::string>());
        const DecodedNodeKind decoded_kind = decode_node_kind(n["kind"].get<std::string>());
        if (decoded_kind == DecodedNodeKind::Component) {
            node.content = Blueprint::Node::ComponentData{};
        } else if (decoded_kind == DecodedNodeKind::BlueprintInstance) {
            node.content = Blueprint::Node::BlueprintInstanceData{
                decode_node_source(n["source"], interner, parser_registry, local_arena)
            };
        } else {
            node.content = Blueprint::Node::BridgePortData{};
        }

        // Kind-specific allowed-field validation (spec Layer 2):
        // collapsed/source are blueprint_instance-only; component/params are component-only.
        const auto& allowed = decoded_kind == DecodedNodeKind::Component
            ? allowed_component_node_fields()
            : decoded_kind == DecodedNodeKind::BlueprintInstance
                ? allowed_blueprint_instance_node_fields()
                : allowed_bridge_port_node_fields();
        check_allowed_fields(n, allowed, "node");

        if (auto v = read_optional_string(n, "label", ctx)) {
            node.view.name = std::move(*v);
        }

        if (decoded_kind == DecodedNodeKind::Component) {
            require_field(n, "component", &nlohmann::json::is_string, ctx, "string");
            node.semantic.type = interner.intern(n["component"].get<std::string>());
            const std::string component_name = n["component"].get<std::string>();
            if (component_name == "BlueprintInput" || component_name == "BlueprintOutput" || component_name == "BridgePort" || component_name == "LegacyPseudoBridge") {
                throw std::runtime_error("invalid node entry: canonical bridge ports must use kind 'bridge_port'");
            }
            if (const ComponentSpec* type_def = parser_registry.get(std::string(interner.resolve(node.semantic.type)))) {
                node.component().iface = interface_from_type_definition(*type_def, interner);
                // Issue #105: render_hint, content_* are runtime/editor-only
                // (ViewData tier 2).  Hydration is the sole responsibility of
                // editor::hydrate_runtime_node_view_data() — NOT the codec.
            }
        } else if (decoded_kind == DecodedNodeKind::BlueprintInstance) {
            node.semantic.type = node.blueprint_instance().source.blueprint_id();
            if (auto v = read_optional_bool(n, "collapsed", ctx)) {
                node.layout.collapsed = *v;
            }
        } else {
            require_field(n, "exposed_port", &nlohmann::json::is_string, ctx, "string");
            require_field(n, "direction", &nlohmann::json::is_string, ctx, "string");
            require_field(n, "port_type", &nlohmann::json::is_string, ctx, "string");

            const auto exposed_port = interner.intern(n["exposed_port"].get<std::string>());
            const auto direction = decode_bridge_direction(n["direction"].get<std::string>());
            const auto port_type = port_type_from_name(n["port_type"].get<std::string>());
            if (!port_type.has_value()) {
                throw std::runtime_error("invalid node entry: unknown bridge port_type");
            }

            node.semantic.type = interner.intern("BridgePort");
            node.bridge_port() = Blueprint::Node::BridgePortData{
                exposed_port,
                direction,
                *port_type,
            };
        }

        const ComponentSpec* type_def = parser_registry.get(std::string(interner.resolve(node.semantic.type)));
        if (n.contains("params")) {
            if (decoded_kind != DecodedNodeKind::Component) {
                throw std::runtime_error("invalid node entry: params only allowed on component nodes");
            }
            if (!n["params"].is_object()) {
                throw std::runtime_error("invalid node entry: params must be object");
            }
            for (auto& [key, val] : n["params"].items()) {
                if (!type_def) {
                    throw std::runtime_error("invalid node entry: params require known node type");
                }
                const auto& type_params = spec_params(*type_def);
                auto schema_it = type_params.find(key);
                if (schema_it == type_params.end()) {
                    throw std::runtime_error("invalid node entry: unknown param '" + key + "'");
                }
                assign_param_by_descriptor(node, interner, key, val, schema_it->second, type_def);
            }
        }

        // Issue #88 Gap #2: Validate that all required parameters are present
        if (decoded_kind == DecodedNodeKind::Component && type_def) {
            const auto& type_params = spec_params(*type_def);
            for (const auto& [param_key, param_spec] : type_params) {
                if (param_spec.required && !param_spec.visual_only) {
                    // Check if param is present in the JSON or was assigned
                    bool param_found = false;
                    if (n.contains("params") && n["params"].contains(param_key)) {
                        param_found = true;
                    } else {
                        // Check if it was already assigned
                        ui::InternedId key_iid = interner.intern(param_key);
                        if (node.semantic.params.count(key_iid) > 0 ||
                            node.semantic.string_params.count(param_key) > 0) {
                            param_found = true;
                        }
                    }
                    if (!param_found) {
                        throw std::runtime_error("invalid node entry: required param '" + param_key + "' missing");
                    }
                }
            }
        }

        const auto& layout = n["layout"];
        check_allowed_fields(layout, allowed_layout_fields(), "layout");
        require_field(layout, "x", &nlohmann::json::is_number, "invalid node entry: layout", "number");
        require_field(layout, "y", &nlohmann::json::is_number, "invalid node entry: layout", "number");
        node.layout.x = parse_finite_float(layout["x"], "layout.x");
        node.layout.y = parse_finite_float(layout["y"], "layout.y");
        if (auto v = read_optional_float(layout, "width", "invalid node entry: layout")) node.layout.width = *v;
        if (auto v = read_optional_float(layout, "height", "invalid node entry: layout")) node.layout.height = *v;
        if (auto v = read_optional_bool(layout, "manual_size", "invalid node entry: layout")) node.layout.manual_size = *v;
        if (layout.contains("port_overrides")) {
            if (!layout["port_overrides"].is_array()) {
                throw std::runtime_error("invalid node entry: layout.port_overrides must be array");
            }
            for (auto const& override : layout["port_overrides"]) {
                if (!override.is_object()) {
                    throw std::runtime_error("invalid node entry: port override must be object");
                }
                check_allowed_fields(override, allowed_layout_override_fields(), "port_overrides");
                require_field(override, "port_id", &nlohmann::json::is_string, "invalid node entry: port_overrides", "string");
                Blueprint::Node::PortLayoutOverride parsed;
                parsed.port_name = override["port_id"].get<std::string>();
                if (override.contains("side")) {
                    if (!override["side"].is_string()) {
                        throw std::runtime_error("invalid node entry: port_overrides.side must be string");
                    }
                    parsed.side = override["side"].get<std::string>();
                }
                if (override.contains("position")) {
                    if (!override["position"].is_number_integer()) {
                        throw std::runtime_error("invalid node entry: port_overrides.position must be integer");
                    }
                    parsed.position = override["position"].get<int>();
                }
                node.layout.layout_overrides.push_back(std::move(parsed));
            }
        }

        bp = bp.with_node(std::move(node));
    }
    return bp;
}

Blueprint decode_wires(Blueprint bp,
                       nlohmann::json const& arr,
                       ui::StringInterner& interner) {
    for (auto const& w : arr) {
        if (!w.is_object()) {
            throw std::runtime_error("invalid wire entry: expected object");
        }
        check_allowed_fields(w, allowed_wire_fields(), "wire");
        require_field(w, "id", &nlohmann::json::is_string, "invalid wire entry", "string");
        require_field(w, "from", &nlohmann::json::is_object, "invalid wire entry", "object");
        require_field(w, "to", &nlohmann::json::is_object, "invalid wire entry", "object");

        Blueprint::Wire wire;
        wire.id = interner.intern(w["id"].get<std::string>());
        wire.source = decode_endpoint(w["from"], interner);
        wire.target = decode_endpoint(w["to"], interner);

        if (w.contains("routing")) {
            if (!w["routing"].is_array()) {
                throw std::runtime_error("invalid wire entry: routing must be array");
            }
            for (auto const& point : w["routing"]) {
                if (!point.is_array() || point.size() != 2 || !point[0].is_number() || !point[1].is_number()) {
                    throw std::runtime_error("invalid wire entry: routing points must be [x,y] pairs");
                }
                wire.routing_points.emplace_back(
                    parse_finite_float(point[0], "routing.x"),
                    parse_finite_float(point[1], "routing.y"));
            }
        }

        bp = bp.with_wire(std::move(wire));
    }
    return bp;
}

Blueprint resolve_wire_domains(Blueprint bp,
                               ::ComponentRegistry const& parser_registry,
                               ui::StringInterner& interner) {
    PathResolver resolver;
    Blueprint result = bp;

    // Clear existing wires and re-add them with resolved domains.
    for (auto const& w : bp.wires()) {
        result = result.without_wire(w.id);
    }
    for (auto const& w : bp.wires()) {
        auto src = resolver.resolve(w.source, bp, parser_registry, interner);
        auto tgt = resolver.resolve(w.target, bp, parser_registry, interner);
        if (!src || !tgt) {
            // Unresolvable wire — keep original domain; invariant checker
            // will report the real error.
            result = result.with_wire(w);
            continue;
        }

        Blueprint::Wire fixed = w;
        const auto resolved = resolve_signal_typing(bp, &parser_registry, interner, w.source, w.target);
        if (resolved.resolved.has_value()) {
            fixed.domain = resolved.resolved->domain;
        }
        result = result.with_wire(std::move(fixed));
    }
    return result;
}

} // namespace bp2::codec_detail

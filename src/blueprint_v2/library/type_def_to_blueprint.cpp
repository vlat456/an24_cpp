#include "type_def_to_blueprint.h"

#include "blueprint_v2/validation/signal_typing.h"
#include "parse_number.h"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace bp2 {

namespace {

/// Parse a "node.port" connection endpoint string into (node, port) pair.
/// Throws std::runtime_error if the string does not contain exactly one '.'.
std::pair<std::string, std::string> parse_endpoint(const std::string& ep,
                                                    const std::string& context) {
    auto dot = ep.find('.');
    if (dot == std::string::npos || dot == 0 || dot == ep.size() - 1) {
        throw std::runtime_error(
            "blueprint_from_type_definition: malformed endpoint '" + ep
            + "' in " + context + " (expected 'node.port')");
    }
    return {ep.substr(0, dot), ep.substr(dot + 1)};
}

Domain resolve_wire_domain(const Blueprint& bp,
                           const ComponentRegistry& registry,
                           ui::StringInterner& interner,
                           const std::string& src_node,
                           const std::string& src_port,
                           const std::string& tgt_node,
                           const std::string& tgt_port,
                           const std::string& context) {
    const auto* src = bp.find_node(interner.lookup(src_node));
    const auto* tgt = bp.find_node(interner.lookup(tgt_node));
    if (!src || !tgt) {
        throw std::runtime_error(
            "blueprint_from_type_definition: unresolved wire node in " + context);
    }

    const auto src_desc = bp.resolve_node_iface(*src, bp2::Blueprint::NodeIfaceAuthority{interner, &registry}).find(interner.lookup(src_port));
    const auto tgt_desc = bp.resolve_node_iface(*tgt, bp2::Blueprint::NodeIfaceAuthority{interner, &registry}).find(interner.lookup(tgt_port));
    if (!src_desc || !tgt_desc) {
        throw std::runtime_error(
            "blueprint_from_type_definition: unresolved wire port in " + context);
    }

    if (port_types_compatible(*src_desc, *tgt_desc)) {
        const auto resolved = resolve_signal_typing(
            bp,
            &registry,
            interner,
            WireEndpoint{interner.lookup(src_node), interner.lookup(src_port)},
            WireEndpoint{interner.lookup(tgt_node), interner.lookup(tgt_port)});
        if (resolved.resolved.has_value()) {
            return resolved.resolved->domain;
        }
    }

    // Both concrete types — domains should match.
    if (src_desc->domain != tgt_desc->domain) {
        throw std::runtime_error(
            "blueprint_from_type_definition: domain mismatch in " + context
            + " (wire " + src_node + "." + src_port + " -> " + tgt_node + "." + tgt_port + ")");
    }
    return src_desc->domain;
}

Blueprint::Node make_bridge_node(const BridgePortDefinition& bridge,
                                 ui::StringInterner& interner) {
    Blueprint::Node node;
    node.semantic.id = interner.intern(bridge.id);
    node.semantic.type = interner.intern("BridgePort");
    node.view.name = bridge.label;

    const bool is_input = bridge.direction == bp2::BridgeDirection::Input;
    node.content = Blueprint::Node::BridgePortData{
        interner.intern(bridge.exposed_port),
        bridge.direction,
        bridge.type,
    };

    if (bridge.pos) {
        node.layout.x = bridge.pos->first;
        node.layout.y = bridge.pos->second;
    }
    if (bridge.size) {
        node.layout.width = bridge.size->first;
        node.layout.height = bridge.size->second;
    }
    node.layout.collapsed = true;
    return node;
}

} // namespace

Blueprint blueprint_from_type_definition(const ComponentSpec& spec,
                                         ui::StringInterner& interner,
                                         const ComponentRegistry& registry) {
    const auto* comp = as_composite(spec);
    if (!comp) {
        throw std::runtime_error(
            "blueprint_from_type_definition: '" + spec_classname(spec)
            + "' is not a composite blueprint");
    }

    Blueprint bp;
    bp = bp.with_id(interner.intern(comp->classname));
    bp = bp.with_name(comp->classname);
    bp = bp.with_interface(interface_from_type_definition(spec, interner));

    // --- Nodes from devices ---
    for (const auto& dev : comp->devices) {
        Blueprint::Node node;
        node.content = Blueprint::Node::ComponentData{};
        node.semantic.id = interner.intern(dev.name);
        node.semantic.type = interner.intern(dev.classname);

        // Numeric params
        for (const auto& [k, v] : dev.params) {
            float fval = 0.0f;
            if (locale_safe::parse_float(v, fval)) {
                node.semantic.params[interner.intern(k)] = fval;
            } else {
                node.semantic.string_params[k] = v;
            }
        }

        // Build node interface from the canonical component definition.
        // The registry is the sole authority for component-node interfaces.
        const ComponentSpec* dev_def = registry.get(dev.classname);
        if (!dev_def) {
            throw std::runtime_error(
                "blueprint_from_type_definition: unknown device class '" + dev.classname
                + "' in composite '" + comp->classname + "'");
        }
        node.component().iface = interface_from_type_definition(*dev_def, interner);

        // Layout from position/size if available
        if (dev.pos) {
            node.layout.x = dev.pos->first;
            node.layout.y = dev.pos->second;
        }
        if (dev.size) {
            node.layout.width = dev.size->first;
            node.layout.height = dev.size->second;
        }
        node.layout.collapsed = true;

        bp = bp.with_node(std::move(node));
    }

    for (const auto& bridge : comp->bridge_ports) {
        bp = bp.with_node(make_bridge_node(bridge, interner));
    }

    // --- Wires from connections ---
    int wire_idx = 0;
    for (const auto& conn : comp->connections) {
        auto [src_node, src_port] = parse_endpoint(conn.from, comp->classname);
        auto [tgt_node, tgt_port] = parse_endpoint(conn.to, comp->classname);

        Blueprint::Wire wire;
        wire.id = interner.intern("w_td_" + std::to_string(wire_idx++));
        wire.source = WireEndpoint{interner.intern(src_node), interner.intern(src_port)};
        wire.target = WireEndpoint{interner.intern(tgt_node), interner.intern(tgt_port)};
        wire.domain = resolve_wire_domain(bp, registry, interner,
                                          src_node, src_port,
                                          tgt_node, tgt_port,
                                          comp->classname);
        wire.routing_points = conn.routing_points;

        bp = bp.with_wire(std::move(wire));
    }

    spdlog::debug("[type_def_to_blueprint] Built blueprint '{}': {} nodes, {} wires",
                  comp->classname, bp.nodes().size(), bp.wires().size());

    return bp;
}

} // namespace bp2

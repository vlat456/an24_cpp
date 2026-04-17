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
                           const TypeRegistry& registry,
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

    const auto src_desc = bp.effective_node_iface(*src).find(interner.lookup(src_port));
    const auto tgt_desc = bp.effective_node_iface(*tgt).find(interner.lookup(tgt_port));
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

} // namespace

Blueprint blueprint_from_type_definition(const TypeDefinition& def,
                                         ui::StringInterner& interner,
                                         const TypeRegistry& registry) {
    if (def.cpp_class) {
        throw std::runtime_error(
            "blueprint_from_type_definition: '" + def.classname
            + "' is a cpp_class, not a composite blueprint");
    }

    Blueprint bp;
    bp = bp.with_id(interner.intern(def.classname));
    bp = bp.with_name(def.classname);
    bp = bp.with_interface(interface_from_type_definition(def, interner));

    // --- Nodes from devices ---
    for (const auto& dev : def.devices) {
        Blueprint::Node node;
        node.kind = Blueprint::Node::Kind::Component;
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

        // Build node interface from TypeRegistry definition for this device's class.
        // The v3 parser does not populate DeviceInstance.ports, so we look up the
        // canonical type definition to get the port list.
        const TypeDefinition* dev_def = registry.get(dev.classname);
        if (dev_def) {
            node.semantic.iface = interface_from_type_definition(*dev_def, interner);
        } else if (!dev.ports.empty()) {
            // Fallback: use device-level ports if somehow populated
            std::vector<PortDescriptor> node_ports;
            node_ports.reserve(dev.ports.size());
            for (const auto& [pname, port] : dev.ports) {
                node_ports.push_back(
                    port_descriptor_from_type_port(interner.intern(pname), port));
            }
            node.semantic.iface = Interface(std::move(node_ports));
        }

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

    // --- Wires from connections ---
    int wire_idx = 0;
    for (const auto& conn : def.connections) {
        auto [src_node, src_port] = parse_endpoint(conn.from, def.classname);
        auto [tgt_node, tgt_port] = parse_endpoint(conn.to, def.classname);

        Blueprint::Wire wire;
        wire.id = interner.intern("w_td_" + std::to_string(wire_idx++));
        wire.source = WireEndpoint{interner.intern(src_node), interner.intern(src_port)};
        wire.target = WireEndpoint{interner.intern(tgt_node), interner.intern(tgt_port)};
        wire.domain = resolve_wire_domain(bp, registry, interner,
                                          src_node, src_port,
                                          tgt_node, tgt_port,
                                          def.classname);
        wire.routing_points = conn.routing_points;

        bp = bp.with_wire(std::move(wire));
    }

    spdlog::debug("[type_def_to_blueprint] Built blueprint '{}': {} nodes, {} wires",
                  def.classname, bp.nodes().size(), bp.wires().size());

    return bp;
}

} // namespace bp2

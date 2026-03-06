#include "blueprint.h"
#include "port.h"

/// Check if two port types are compatible for connection
static bool are_ports_compatible(an24::PortType from_type, an24::PortType to_type) {
    // Any type is wildcard - compatible with everything
    if (from_type == an24::PortType::Any || to_type == an24::PortType::Any) {
        return true;
    }
    // Types must match exactly
    return from_type == to_type;
}

/// Find a port in a node's inputs or outputs
static an24::PortType find_port_type(const Node& node, const std::string& port_name) {
    // Search inputs
    for (const auto& port : node.inputs) {
        if (port.name == port_name) {
            return port.type;
        }
    }
    // Search outputs
    for (const auto& port : node.outputs) {
        if (port.name == port_name) {
            return port.type;
        }
    }
    // Default to Any if not found
    return an24::PortType::Any;
}

bool Blueprint::add_wire_validated(Wire wire) {
    // Find the start and end nodes
    Node* start_node = nullptr;
    Node* end_node = nullptr;

    for (auto& n : nodes) {
        if (n.id == wire.start.node_id) {
            start_node = &n;
        }
        if (n.id == wire.end.node_id) {
            end_node = &n;
        }
    }

    // If either node doesn't exist, don't add the wire
    if (!start_node || !end_node) {
        return false;
    }

    // Get port types
    an24::PortType start_type = find_port_type(*start_node, wire.start.port_name);
    an24::PortType end_type = find_port_type(*end_node, wire.end.port_name);

    // Check compatibility
    if (!are_ports_compatible(start_type, end_type)) {
        // Types are incompatible - reject the connection
        return false;
    }

    // Types are compatible - add the wire
    wires.push_back(std::move(wire));
    return true;
}

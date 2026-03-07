#include "blueprint.h"
#include "port.h"
#include <set>
#include <cstdio>

static const char* port_type_cstr(an24::PortType t) {
    switch (t) {
        case an24::PortType::V: return "V";
        case an24::PortType::I: return "I";
        case an24::PortType::Bool: return "Bool";
        case an24::PortType::RPM: return "RPM";
        case an24::PortType::Temperature: return "Temp";
        case an24::PortType::Pressure: return "Press";
        case an24::PortType::Position: return "Pos";
        default: return "Any";
    }
}

static const char* port_side_cstr(PortSide s) {
    switch (s) {
        case PortSide::Input: return "In";
        case PortSide::Output: return "Out";
        case PortSide::InOut: return "InOut";
        default: return "?";
    }
}

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
    fprintf(stderr, "[WIRE] add_wire_validated: %s.%s(%s) -> %s.%s(%s)\n",
            wire.start.node_id.c_str(), wire.start.port_name.c_str(), port_side_cstr(wire.start.side),
            wire.end.node_id.c_str(), wire.end.port_name.c_str(), port_side_cstr(wire.end.side));

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
        fprintf(stderr, "[WIRE]   REJECTED: node not found (start=%p end=%p)\n",
                (void*)start_node, (void*)end_node);
        return false;
    }

    fprintf(stderr, "[WIRE]   start_node: id=%s type=%s  end_node: id=%s type=%s\n",
            start_node->id.c_str(), start_node->type_name.c_str(),
            end_node->id.c_str(), end_node->type_name.c_str());

    // Get port types
    an24::PortType start_type = find_port_type(*start_node, wire.start.port_name);
    an24::PortType end_type = find_port_type(*end_node, wire.end.port_name);

    fprintf(stderr, "[WIRE]   port types: start=%s end=%s\n",
            port_type_cstr(start_type), port_type_cstr(end_type));

    // Check compatibility
    if (!are_ports_compatible(start_type, end_type)) {
        fprintf(stderr, "[WIRE]   REJECTED: port types incompatible\n");
        return false;
    }

    // Check one-to-one connections (except for Bus/RefNode)
    bool start_allows_multiple = (start_node->type_name == "Bus" ||
                                   start_node->type_name == "RefNode");
    bool end_allows_multiple = (end_node->type_name == "Bus" ||
                                 end_node->type_name == "RefNode");

    fprintf(stderr, "[WIRE]   allows_multiple: start=%d end=%d\n",
            start_allows_multiple, end_allows_multiple);
    fprintf(stderr, "[WIRE]   existing wires (%zu):\n", wires.size());
    for (size_t i = 0; i < wires.size(); i++) {
        fprintf(stderr, "[WIRE]     [%zu] %s: %s.%s(%s) -> %s.%s(%s)\n", i,
                wires[i].id.c_str(),
                wires[i].start.node_id.c_str(), wires[i].start.port_name.c_str(), port_side_cstr(wires[i].start.side),
                wires[i].end.node_id.c_str(), wires[i].end.port_name.c_str(), port_side_cstr(wires[i].end.side));
    }

    // Check if start port is already occupied
    if (!start_allows_multiple) {
        for (const auto& w : wires) {
            if (w.start.node_id == wire.start.node_id &&
                w.start.port_name == wire.start.port_name) {
                fprintf(stderr, "[WIRE]   REJECTED: start port occupied by wire %s (w.start=%s.%s)\n",
                        w.id.c_str(), w.start.node_id.c_str(), w.start.port_name.c_str());
                return false;
            }
            if (w.end.node_id == wire.start.node_id &&
                w.end.port_name == wire.start.port_name) {
                fprintf(stderr, "[WIRE]   REJECTED: start port occupied by wire %s (w.end=%s.%s)\n",
                        w.id.c_str(), w.end.node_id.c_str(), w.end.port_name.c_str());
                return false;
            }
        }
    }

    // Check if end port is already occupied
    if (!end_allows_multiple) {
        for (const auto& w : wires) {
            if (w.end.node_id == wire.end.node_id &&
                w.end.port_name == wire.end.port_name) {
                fprintf(stderr, "[WIRE]   REJECTED: end port occupied by wire %s (w.end=%s.%s)\n",
                        w.id.c_str(), w.end.node_id.c_str(), w.end.port_name.c_str());
                return false;
            }
            if (w.start.node_id == wire.end.node_id &&
                w.start.port_name == wire.end.port_name) {
                fprintf(stderr, "[WIRE]   REJECTED: end port occupied by wire %s (w.start=%s.%s)\n",
                        w.id.c_str(), w.start.node_id.c_str(), w.start.port_name.c_str());
                return false;
            }
        }
    }

    // All checks passed - add the wire
    fprintf(stderr, "[WIRE]   ACCEPTED\n");
    wires.push_back(std::move(wire));
    return true;
}

#include "blueprint.h"
#include "port.h"
#include "layout_constants.h"
#include <set>
#include <cstdio>
#include <spdlog/spdlog.h>

// BUGFIX [e4a1b7] Runtime dedup: reject exact-duplicate wires with warning
// [2.1] O(1) set-based dedup replaces O(n) linear scan
size_t Blueprint::add_wire(Wire wire) {
    WireKey key(wire);
    if (!wire_index_.insert(key).second) {
        spdlog::warn("[dedup] Runtime duplicate wire rejected: {}.{} → {}.{}",
            wire.start.node_id, wire.start.port_name,
            wire.end.node_id, wire.end.port_name);
        return SIZE_MAX;
    }
    size_t idx = wires.size();
    wires.push_back(std::move(wire));
    return idx;
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

// [SMELL-k1l2] Removed 17 fprintf debug lines — use spdlog for diagnostic logging instead
bool Blueprint::add_wire_validated(Wire wire) {
    // Find the start and end nodes
    Node* start_node = nullptr;
    Node* end_node = nullptr;

    for (auto& n : nodes) {
        if (n.id == wire.start.node_id) start_node = &n;
        if (n.id == wire.end.node_id) end_node = &n;
    }

    if (!start_node || !end_node) return false;

    // Get port types
    an24::PortType start_type = find_port_type(*start_node, wire.start.port_name);
    an24::PortType end_type = find_port_type(*end_node, wire.end.port_name);

    // Check compatibility
    if (!are_ports_compatible(start_type, end_type)) return false;

    // Check one-to-one connections (except for Bus/RefNode)
    bool start_allows_multiple = (start_node->type_name == "Bus" ||
                                   start_node->type_name == "RefNode");
    bool end_allows_multiple = (end_node->type_name == "Bus" ||
                                 end_node->type_name == "RefNode");

    // Check if start port is already occupied
    if (!start_allows_multiple) {
        for (const auto& w : wires) {
            if ((w.start.node_id == wire.start.node_id && w.start.port_name == wire.start.port_name) ||
                (w.end.node_id == wire.start.node_id && w.end.port_name == wire.start.port_name))
                return false;
        }
    }

    // Check if end port is already occupied
    if (!end_allows_multiple) {
        for (const auto& w : wires) {
            if ((w.end.node_id == wire.end.node_id && w.end.port_name == wire.end.port_name) ||
                (w.start.node_id == wire.end.node_id && w.start.port_name == wire.end.port_name))
                return false;
        }
    }

    wires.push_back(std::move(wire));
    wire_index_.insert(WireKey(wires.back()));
    return true;
}

// ─── Auto-layout for a single group ───

static Pt layout_snap(Pt p) {
    return Pt(std::round(p.x / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID,
              std::round(p.y / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID);
}

void Blueprint::auto_layout_group(const std::string& group_id) {
    // BUGFIX [a2d7c5] BlueprintInput → leftmost column, BlueprintOutput → rightmost
    // Collect indices of nodes in this group by role
    std::vector<size_t> bp_inputs, sources, buses, loads, grounds, bp_outputs;
    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes[i].group_id != group_id) continue;
        const auto& tn = nodes[i].type_name;
        if (tn == "BlueprintInput")
            bp_inputs.push_back(i);
        else if (tn == "BlueprintOutput")
            bp_outputs.push_back(i);
        else if (nodes[i].render_hint == "ref")
            grounds.push_back(i);
        else if (nodes[i].render_hint == "bus" || tn == "Bus")
            buses.push_back(i);
        else if (tn == "Battery" || tn == "Generator")
            sources.push_back(i);
        else
            loads.push_back(i);
    }

    if (bp_inputs.empty() && sources.empty() && buses.empty() &&
        loads.empty() && grounds.empty() && bp_outputs.empty())
        return;

    constexpr float col_spacing = editor_constants::LAYOUT_COL_SPACING;
    constexpr float row_spacing = editor_constants::LAYOUT_ROW_SPACING;
    constexpr float origin_x = editor_constants::LAYOUT_ORIGIN_X;
    constexpr float origin_y = editor_constants::LAYOUT_ORIGIN_Y;

    // Columns left→right: BlueprintInput | Sources | Buses | Loads | BlueprintOutput
    float col = origin_x;
    float bpi_x = col;   if (!bp_inputs.empty())  col += col_spacing;
    float src_x = col;   if (!sources.empty())    col += col_spacing;
    float bus_x = col;   if (!buses.empty())      col += col_spacing;
    float load_x = col;  if (!loads.empty())      col += col_spacing;
    float bpo_x = col;

    for (size_t i = 0; i < bp_inputs.size(); i++)
        nodes[bp_inputs[i]].pos = layout_snap(Pt(bpi_x, origin_y + i * row_spacing));
    for (size_t i = 0; i < sources.size(); i++)
        nodes[sources[i]].pos = layout_snap(Pt(src_x, origin_y + i * row_spacing));
    for (size_t i = 0; i < buses.size(); i++)
        nodes[buses[i]].pos = layout_snap(Pt(bus_x, origin_y + i * row_spacing));
    for (size_t i = 0; i < loads.size(); i++)
        nodes[loads[i]].pos = layout_snap(Pt(load_x, origin_y + i * row_spacing));
    for (size_t i = 0; i < bp_outputs.size(); i++)
        nodes[bp_outputs[i]].pos = layout_snap(Pt(bpo_x, origin_y + i * row_spacing));

    size_t max_rows = std::max({bp_inputs.size(), sources.size(), buses.size(),
                                loads.size(), bp_outputs.size(), size_t(1)});
    float ground_y = origin_y + max_rows * row_spacing;
    for (size_t i = 0; i < grounds.size(); i++)
        nodes[grounds[i]].pos = layout_snap(Pt(bus_x, ground_y + i * row_spacing));
}

// ─── Shared blueprint expansion ───

Blueprint expand_type_definition(const an24::TypeDefinition& def, const an24::TypeRegistry& registry) {
    using namespace an24;
    Blueprint bp;

    for (const auto& dev : def.devices) {
        Node node;
        node.id = dev.name;
        node.name = dev.name;
        node.type_name = dev.classname;

        // Position and size from stored layout (editor-saved blueprints)
        if (dev.pos)
            node.pos = Pt(dev.pos->first, dev.pos->second);
        if (dev.size)
            node.size = Pt(dev.size->first, dev.size->second);
        else
            node.size = get_default_node_size(dev.classname, &registry);

        // Type info from registry
        const auto* inner_def = registry.get(dev.classname);
        if (inner_def) {
            node.render_hint = inner_def->render_hint;
            node.expandable = !inner_def->cpp_class && !inner_def->devices.empty();
            node.node_content = create_node_content_from_def(inner_def);
        }

        // Ports: prefer inline ports from device, fall back to registry
        const auto& port_source = !dev.ports.empty() ? dev.ports
                                : (inner_def ? inner_def->ports : dev.ports);
        for (const auto& [port_name, port] : port_source) {
            if (port.direction == PortDirection::In || port.direction == PortDirection::InOut)
                node.inputs.emplace_back(port_name.c_str(), PortSide::Input, port.type);
            if (port.direction == PortDirection::Out || port.direction == PortDirection::InOut)
                node.outputs.emplace_back(port_name.c_str(), PortSide::Output, port.type);
        }

        // Params: registry defaults + device overrides
        if (inner_def)
            node.params = inner_def->params;
        for (const auto& [k, v] : dev.params)
            node.params[k] = v;

        bp.add_node(std::move(node));
    }

    for (const auto& conn : def.connections) {
        Wire wire;
        wire.id = conn.from + "->" + conn.to;

        size_t dot = conn.from.find('.');
        if (dot != std::string::npos) {
            wire.start.node_id = conn.from.substr(0, dot);
            wire.start.port_name = conn.from.substr(dot + 1);
        }
        dot = conn.to.find('.');
        if (dot != std::string::npos) {
            wire.end.node_id = conn.to.substr(0, dot);
            wire.end.port_name = conn.to.substr(dot + 1);
        }

        for (const auto& [x, y] : conn.routing_points)
            wire.routing_points.push_back(Pt(x, y));

        bp.add_wire(std::move(wire));
    }

    return bp;
}

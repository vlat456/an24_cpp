#include "commands.h"
#include "data/blueprint.h"
#include <spdlog/spdlog.h>
#include <algorithm>

// =============================================================================
// Execute implementation for each command type (void — no inverse)
// =============================================================================

static void execute(Blueprint& bp, const CmdAddNode& cmd) {
    bp.add_node(Node{cmd.node});  // Copy

    // If node belongs to a group, restore its internal_node_ids entry
    if (!cmd.node.group_id.empty()) {
        std::string node_id_str(bp.interner().resolve(cmd.node.id));
        for (auto& g : bp.sub_blueprint_instances) {
            if (g.id == cmd.node.group_id) {
                if (std::find(g.internal_node_ids.begin(), g.internal_node_ids.end(), node_id_str)
                    == g.internal_node_ids.end()) {
                    g.internal_node_ids.push_back(node_id_str);
                }
                break;
            }
        }
    }
}

static void execute(Blueprint& bp, const CmdRemoveNode& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[cmd] CmdRemoveNode: node {} not found", cmd.node_id.raw());
        return;
    }

    size_t idx = it->second;

    // Remove connected wires
    bp.wires.erase(
        std::remove_if(bp.wires.begin(), bp.wires.end(),
            [&](const Wire& w) {
                return w.start.node_id == cmd.node_id || w.end.node_id == cmd.node_id;
            }),
        bp.wires.end());

    // Remove node
    bp.nodes.erase(bp.nodes.begin() + idx);

    // Clean up internal_node_ids in sub-blueprint instances
    std::string node_id_str(bp.interner().resolve(cmd.node_id));
    for (auto& g : bp.sub_blueprint_instances) {
        g.internal_node_ids.erase(
            std::remove(g.internal_node_ids.begin(), g.internal_node_ids.end(), node_id_str),
            g.internal_node_ids.end());
    }

    // Rebuild indices
    bp.rebuild_node_index();
    bp.rebuild_wire_index();
    bp.rebuild_wire_id_index();
    bp.rebuild_bus_wire_index();
    bp.rebuild_port_occupancy_index();
}

static void execute(Blueprint& bp, const CmdMoveNode& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[cmd] CmdMoveNode: node {} not found", cmd.node_id.raw());
        return;
    }
    bp.nodes[it->second].pos = cmd.new_pos;
}

static void execute(Blueprint& bp, const CmdAddWire& cmd) {
    bp.add_wire(Wire{cmd.wire});
}

static void execute(Blueprint& bp, const CmdRemoveWire& cmd) {
    auto it = bp.wire_id_index_.find(cmd.wire_id);
    if (it == bp.wire_id_index_.end()) {
        spdlog::warn("[cmd] CmdRemoveWire: wire {} not found", cmd.wire_id.raw());
        return;
    }

    bp.wires.erase(bp.wires.begin() + it->second);
    bp.rebuild_wire_index();
    bp.rebuild_wire_id_index();
    bp.rebuild_port_occupancy_index();
}

static void execute(Blueprint& bp, const CmdReconnectWire& cmd) {
    auto it = bp.wire_id_index_.find(cmd.wire_id);
    if (it == bp.wire_id_index_.end()) {
        spdlog::warn("[cmd] CmdReconnectWire: wire {} not found", cmd.wire_id.raw());
        return;
    }

    Wire& wire = bp.wires[it->second];
    Wire old_wire = wire;  // snapshot before modification

    WireEnd& endpoint = cmd.is_start ? wire.start : wire.end;
    endpoint.node_id = cmd.new_node_id;
    endpoint.port_name = cmd.new_port_name;

    // Update derived indices to reflect the changed endpoints
    bp.rekey_wire(old_wire, wire);
    bp.updateBusIndexForEndpoints(old_wire, wire);
    bp.updatePortOccupancyForEndpoints(old_wire, wire);
}

static void execute(Blueprint& bp, const CmdSetParam& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[cmd] CmdSetParam: node {} not found", cmd.node_id.raw());
        return;
    }
    bp.nodes[it->second].params[cmd.key] = cmd.new_value;
}

static void execute(Blueprint& bp, const CmdResizeNode& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[cmd] CmdResizeNode: node {} not found", cmd.node_id.raw());
        return;
    }
    Node& node = bp.nodes[it->second];
    node.pos = cmd.new_pos;
    node.size = cmd.new_size;
}

static void execute(Blueprint& bp, const CmdSetRoutingPoints& cmd) {
    auto it = bp.wire_id_index_.find(cmd.wire_id);
    if (it == bp.wire_id_index_.end()) {
        spdlog::warn("[cmd] CmdSetRoutingPoints: wire {} not found", cmd.wire_id.raw());
        return;
    }
    bp.wires[it->second].routing_points = cmd.new_points;
}

static void execute(Blueprint& bp, const CmdSetGridStep& cmd) {
    bp.grid_step = cmd.new_step;
}

static void execute(Blueprint& bp, const CmdSetName& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[cmd] CmdSetName: node {} not found", cmd.node_id.raw());
        return;
    }
    bp.nodes[it->second].name = cmd.new_name;
}

static void execute(Blueprint& bp, const CmdSwapBusPorts& cmd) {
    const Wire* wa = bp.find_wire(cmd.wire_id_a);
    const Wire* wb = bp.find_wire(cmd.wire_id_b);

    if (!wa || !wb) {
        spdlog::warn("[cmd] CmdSwapBusPorts: wire not found");
        return;
    }

    size_t idx_a = static_cast<size_t>(wa - bp.wires.data());
    size_t idx_b = static_cast<size_t>(wb - bp.wires.data());

    std::swap(bp.wires[idx_a], bp.wires[idx_b]);
    bp.rebuild_wire_id_index();
}

static void execute(Blueprint& bp, const CmdAddSubBlueprint& cmd) {
    bp.sub_blueprint_instances.push_back(cmd.instance);
}

static void execute(Blueprint& bp, const CmdRemoveSubBlueprint& cmd) {
    auto it = std::find_if(bp.sub_blueprint_instances.begin(),
                           bp.sub_blueprint_instances.end(),
                           [&](const SubBlueprintInstance& sbi) {
                               return sbi.id == cmd.instance_id;
                           });

    if (it == bp.sub_blueprint_instances.end()) {
        spdlog::warn("[cmd] CmdRemoveSubBlueprint: instance '{}' not found", cmd.instance_id);
        return;
    }

    bp.sub_blueprint_instances.erase(it);
}

// =============================================================================
// Main dispatch
// =============================================================================

void execute(Blueprint& bp, const Command& cmd) {
    std::visit([&](auto&& c) { execute(bp, c); }, cmd);
}

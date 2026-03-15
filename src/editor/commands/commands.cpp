#include "commands.h"
#include "data/blueprint.h"
#include <spdlog/spdlog.h>
#include <algorithm>

// =============================================================================
// Execute implementation for each command type
// =============================================================================

Command execute(Blueprint& bp, const CmdAddNode& cmd) {
    // Store inverse data
    ui::InternedId node_id = cmd.node.id;
    
    // Execute: add node
    bp.add_node(Node{cmd.node});  // Copy
    
    // Return inverse: remove this node
    return CmdRemoveNode{node_id};
}

Command execute(Blueprint& bp, const CmdRemoveNode& cmd) {
    // Find node
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[undo] CmdRemoveNode: node {} not found", cmd.node_id.raw());
        return CmdAddNode{Node{}};  // No-op
    }
    
    size_t idx = it->second;
    Node node_copy = bp.nodes[idx];
    
    // Collect connected wires (copy before mutation)
    std::vector<Wire> wires_copy;
    for (const auto& w : bp.wires) {
        if (w.start.node_id == cmd.node_id || w.end.node_id == cmd.node_id) {
            wires_copy.push_back(w);
        }
    }
    
    // Remove wires via erase-remove (safe, no index invalidation)
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
    bp.rebuild_port_occupancy_index();
    
    // Build inverse: add node back + re-add all connected wires
    if (wires_copy.empty()) {
        return CmdAddNode{node_copy};
    }
    
    std::vector<std::any> inverse_cmds;
    inverse_cmds.reserve(1 + wires_copy.size());
    inverse_cmds.push_back(Command{AtomicCommand{CmdAddNode{node_copy}}});
    for (auto& w : wires_copy) {
        inverse_cmds.push_back(Command{AtomicCommand{CmdAddWire{std::move(w)}}});
    }
    return CmdCompound{std::move(inverse_cmds)};
}

Command execute(Blueprint& bp, const CmdMoveNode& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[undo] CmdMoveNode: node {} not found", cmd.node_id.raw());
        return CmdMoveNode{cmd.node_id, cmd.new_pos};
    }
    
    Node& node = bp.nodes[it->second];
    ui::Pt old_pos = node.pos;
    
    // Execute
    node.pos = cmd.new_pos;
    
    // Return inverse
    return CmdMoveNode{cmd.node_id, old_pos};
}

Command execute(Blueprint& bp, const CmdAddWire& cmd) {
    ui::InternedId wire_id = cmd.wire.id;
    
    // Execute: add wire
    bp.add_wire(Wire{cmd.wire});
    
    // Return inverse
    return CmdRemoveWire{wire_id};
}

Command execute(Blueprint& bp, const CmdRemoveWire& cmd) {
    auto it = bp.wire_id_index_.find(cmd.wire_id);
    if (it == bp.wire_id_index_.end()) {
        spdlog::warn("[undo] CmdRemoveWire: wire {} not found", cmd.wire_id.raw());
        return CmdAddWire{Wire{}};
    }
    
    Wire wire_copy = bp.wires[it->second];
    
    // Execute: remove wire
    bp.wires.erase(bp.wires.begin() + it->second);
    bp.rebuild_wire_index();
    bp.rebuild_wire_id_index();
    bp.rebuild_port_occupancy_index();
    
    // Return inverse
    return CmdAddWire{wire_copy};
}

Command execute(Blueprint& bp, const CmdReconnectWire& cmd) {
    auto it = bp.wire_id_index_.find(cmd.wire_id);
    if (it == bp.wire_id_index_.end()) {
        spdlog::warn("[undo] CmdReconnectWire: wire {} not found", cmd.wire_id.raw());
        return CmdReconnectWire{cmd.wire_id, cmd.new_node_id, cmd.new_port_name, cmd.is_start};
    }
    
    Wire& wire = bp.wires[it->second];
    WireEnd& endpoint = cmd.is_start ? wire.start : wire.end;
    
    // Store old
    ui::InternedId old_node_id = endpoint.node_id;
    ui::InternedId old_port_name = endpoint.port_name;
    
    // Execute
    endpoint.node_id = cmd.new_node_id;
    endpoint.port_name = cmd.new_port_name;
    
    // Return inverse
    return CmdReconnectWire{cmd.wire_id, old_node_id, old_port_name, cmd.is_start};
}

Command execute(Blueprint& bp, const CmdSetParam& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[undo] CmdSetParam: node {} not found", cmd.node_id.raw());
        return CmdSetParam{cmd.node_id, cmd.key, cmd.new_value};
    }
    
    Node& node = bp.nodes[it->second];
    
    // Store old
    std::string old_value;
    auto param_it = node.params.find(cmd.key);
    if (param_it != node.params.end()) {
        old_value = param_it->second;
    }
    
    // Execute
    node.params[cmd.key] = cmd.new_value;
    
    // Return inverse
    return CmdSetParam{cmd.node_id, cmd.key, old_value};
}

Command execute(Blueprint& bp, const CmdResizeNode& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[undo] CmdResizeNode: node {} not found", cmd.node_id.raw());
        return CmdResizeNode{cmd.node_id, cmd.new_pos, cmd.new_size};
    }
    
    Node& node = bp.nodes[it->second];
    
    // Store old
    ui::Pt old_pos = node.pos;
    ui::Pt old_size = node.size;
    
    // Execute
    node.pos = cmd.new_pos;
    node.size = cmd.new_size;
    
    // Return inverse
    return CmdResizeNode{cmd.node_id, old_pos, old_size};
}

Command execute(Blueprint& bp, const CmdSetRoutingPoints& cmd) {
    auto it = bp.wire_id_index_.find(cmd.wire_id);
    if (it == bp.wire_id_index_.end()) {
        spdlog::warn("[undo] CmdSetRoutingPoints: wire {} not found", cmd.wire_id.raw());
        return CmdSetRoutingPoints{cmd.wire_id, cmd.new_points};
    }
    
    Wire& wire = bp.wires[it->second];
    
    // Store old
    std::vector<ui::Pt> old_points = wire.routing_points;
    
    // Execute
    wire.routing_points = cmd.new_points;
    
    // Return inverse
    return CmdSetRoutingPoints{cmd.wire_id, std::move(old_points)};
}

Command execute(Blueprint& bp, const CmdSetGridStep& cmd) {
    float old_step = bp.grid_step;
    bp.grid_step = cmd.new_step;
    return CmdSetGridStep{old_step};
}

Command execute(Blueprint& bp, const CmdSetName& cmd) {
    auto it = bp.node_index_.find(cmd.node_id);
    if (it == bp.node_index_.end()) {
        spdlog::warn("[undo] CmdSetName: node {} not found", cmd.node_id.raw());
        return CmdSetName{cmd.node_id, cmd.new_name};
    }
    
    Node& node = bp.nodes[it->second];
    std::string old_name = node.name;
    
    node.name = cmd.new_name;
    
    return CmdSetName{cmd.node_id, std::move(old_name)};
}

Command execute(Blueprint& bp, const CmdSwapBusPorts& cmd) {
    const Wire* wa = bp.find_wire(cmd.wire_id_a);
    const Wire* wb = bp.find_wire(cmd.wire_id_b);
    
    if (!wa || !wb) {
        spdlog::warn("[undo] CmdSwapBusPorts: wire not found");
        return cmd;  // Return same command as inverse (no-op)
    }
    
    size_t idx_a = static_cast<size_t>(wa - bp.wires.data());
    size_t idx_b = static_cast<size_t>(wb - bp.wires.data());
    
    std::swap(bp.wires[idx_a], bp.wires[idx_b]);
    bp.rebuild_wire_id_index();
    
    return cmd;  // Swap is its own inverse
}

Command execute(Blueprint& bp, const CmdCompound& cmd) {
    std::vector<std::any> inverses;
    inverses.reserve(cmd.commands.size());
    
    for (const auto& c : cmd.commands) {
        Command inner = std::any_cast<Command>(c);
        Command inv = execute(bp, inner);
        inverses.push_back(std::move(inv));
    }
    
    std::reverse(inverses.begin(), inverses.end());
    return CmdCompound{std::move(inverses)};
}

// =============================================================================
// Main dispatch
// =============================================================================

Command execute(Blueprint& bp, const AtomicCommand& cmd) {
    return std::visit([&](auto&& c) -> Command { return execute(bp, c); }, cmd);
}

Command execute(Blueprint& bp, const Command& cmd) {
    return std::visit([&](auto&& c) -> Command { return execute(bp, c); }, cmd);
}

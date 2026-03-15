#pragma once

#include "../../ui/core/interned_id.h"
#include "../../ui/math/pt.h"
#include "data/node.h"
#include "data/wire.h"
#include "data/sub_blueprint_instance.h"
#include <variant>
#include <vector>
#include <string>

// =============================================================================
// Command Pattern — Mutation API (no inverse, no compound)
// =============================================================================
//
// Commands describe mutations to a Blueprint. execute() applies the mutation.
// Undo is handled by the snapshot-based UndoStack (stores full Blueprint copies
// before each mutation), so commands no longer need to compute or return inverses.
//
// Usage:
//   undo_stack.snapshot(bp);           // save state before mutation
//   execute(bp, CmdMoveNode{id, pos}); // apply mutation
//   // To undo: undo_stack.undo(bp);   // restores snapshot

struct Blueprint;

// =============================================================================
// Atomic Commands
// =============================================================================

struct CmdAddNode { Node node; };
struct CmdRemoveNode { ui::InternedId node_id; };
struct CmdMoveNode { ui::InternedId node_id; ui::Pt new_pos; };
struct CmdAddWire { Wire wire; };
struct CmdRemoveWire { ui::InternedId wire_id; };
struct CmdReconnectWire { 
    ui::InternedId wire_id; 
    ui::InternedId new_node_id; 
    ui::InternedId new_port_name; 
    bool is_start; 
};
struct CmdSetParam { 
    ui::InternedId node_id; 
    std::string key; 
    std::string new_value; 
};
struct CmdResizeNode {
    ui::InternedId node_id;
    ui::Pt new_pos;
    ui::Pt new_size;
};
struct CmdSetRoutingPoints { 
    ui::InternedId wire_id; 
    std::vector<ui::Pt> new_points; 
};
struct CmdSetGridStep { float new_step; };
struct CmdSetName { 
    ui::InternedId node_id; 
    std::string new_name; 
};
struct CmdSwapBusPorts {
    ui::InternedId bus_node_id;
    ui::InternedId wire_id_a;
    ui::InternedId wire_id_b;
};
struct CmdAddSubBlueprint { SubBlueprintInstance instance; };
struct CmdRemoveSubBlueprint { std::string instance_id; };
struct CmdSetPortLayout {
    ui::InternedId node_id;
    std::vector<PortLayoutOverride> new_overrides;
};

// =============================================================================
// Command Variant
// =============================================================================

using Command = std::variant<
    CmdAddNode,
    CmdRemoveNode,
    CmdMoveNode,
    CmdAddWire,
    CmdRemoveWire,
    CmdReconnectWire,
    CmdSetParam,
    CmdResizeNode,
    CmdSetRoutingPoints,
    CmdSetGridStep,
    CmdSetName,
    CmdSwapBusPorts,
    CmdAddSubBlueprint,
    CmdRemoveSubBlueprint,
    CmdSetPortLayout
>;

// =============================================================================
// Execute - performs mutation (no inverse returned)
// =============================================================================

void execute(Blueprint& bp, const Command& cmd);

// =============================================================================
// Convenience factories
// =============================================================================

inline Command cmd_add_node(Node n) { return CmdAddNode{std::move(n)}; }
inline Command cmd_remove_node(ui::InternedId id) { return CmdRemoveNode{id}; }
inline Command cmd_move_node(ui::InternedId id, ui::Pt pos) { return CmdMoveNode{id, pos}; }
inline Command cmd_add_wire(Wire w) { return CmdAddWire{std::move(w)}; }
inline Command cmd_remove_wire(ui::InternedId id) { return CmdRemoveWire{id}; }
inline Command cmd_reconnect_wire(ui::InternedId id, ui::InternedId node, ui::InternedId port, bool start) {
    return CmdReconnectWire{id, node, port, start};
}
inline Command cmd_set_param(ui::InternedId id, std::string k, std::string v) { 
    return CmdSetParam{id, std::move(k), std::move(v)}; 
}
inline Command cmd_resize_node(ui::InternedId id, ui::Pt pos, ui::Pt size) {
    return CmdResizeNode{id, pos, size};
}
inline Command cmd_set_routing_points(ui::InternedId id, std::vector<ui::Pt> pts) {
    return CmdSetRoutingPoints{id, std::move(pts)};
}
inline Command cmd_set_grid_step(float step) { return CmdSetGridStep{step}; }
inline Command cmd_set_name(ui::InternedId id, std::string name) { 
    return CmdSetName{id, std::move(name)}; 
}
inline Command cmd_swap_bus_ports(ui::InternedId bus_id, ui::InternedId wire_a, ui::InternedId wire_b) {
    return CmdSwapBusPorts{bus_id, wire_a, wire_b};
}
inline Command cmd_add_sub_blueprint(SubBlueprintInstance sbi) {
    return CmdAddSubBlueprint{std::move(sbi)};
}
inline Command cmd_remove_sub_blueprint(std::string id) {
    return CmdRemoveSubBlueprint{std::move(id)};
}
inline Command cmd_set_port_layout(ui::InternedId node_id, std::vector<PortLayoutOverride> overrides) {
    return CmdSetPortLayout{node_id, std::move(overrides)};
}

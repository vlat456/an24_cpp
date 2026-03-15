#pragma once

#include "../../ui/core/interned_id.h"
#include "../../ui/math/pt.h"
#include "data/node.h"
#include "data/wire.h"
#include <variant>
#include <vector>
#include <string>
#include <memory>

// =============================================================================
// Command Pattern with Automatic Inverse
// =============================================================================
// 
// ALL mutations go through commands. Each command:
// 1. Performs the action on Blueprint
// 2. Returns the inverse command for undo
// 
// Flow:
//   auto cmd = CmdMoveNode{id, new_pos};
//   auto inverse = execute(bp, cmd);  // moves node, returns {id, old_pos}
//   undo_stack.push(inverse);

struct Blueprint;

// =============================================================================
// Atomic Commands (no nesting)
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

// =============================================================================
// Atomic Command Variant
// =============================================================================

using AtomicCommand = std::variant<
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
    CmdSwapBusPorts
>;

// =============================================================================
// Compound Command (type-erased to avoid recursive variant)
// =============================================================================

struct CmdCompound {
    std::vector<std::any> commands;  // Type-erased AtomicCommand or nested CmdCompound
};

// =============================================================================
// Unified Command type
// =============================================================================

using Command = std::variant<AtomicCommand, CmdCompound>;

// =============================================================================
// Execute - performs action, returns inverse
// =============================================================================

[[nodiscard]] Command execute(Blueprint& bp, const AtomicCommand& cmd);
[[nodiscard]] Command execute(Blueprint& bp, const Command& cmd);

// =============================================================================
// Convenience factories
// =============================================================================

inline Command cmd_add_node(Node n) { return AtomicCommand{CmdAddNode{std::move(n)}}; }
inline Command cmd_remove_node(ui::InternedId id) { return AtomicCommand{CmdRemoveNode{id}}; }
inline Command cmd_move_node(ui::InternedId id, ui::Pt pos) { return AtomicCommand{CmdMoveNode{id, pos}}; }
inline Command cmd_add_wire(Wire w) { return AtomicCommand{CmdAddWire{std::move(w)}}; }
inline Command cmd_remove_wire(ui::InternedId id) { return AtomicCommand{CmdRemoveWire{id}}; }
inline Command cmd_reconnect_wire(ui::InternedId id, ui::InternedId node, ui::InternedId port, bool start) {
    return AtomicCommand{CmdReconnectWire{id, node, port, start}};
}
inline Command cmd_set_param(ui::InternedId id, std::string k, std::string v) { 
    return AtomicCommand{CmdSetParam{id, std::move(k), std::move(v)}}; 
}
inline Command cmd_resize_node(ui::InternedId id, ui::Pt pos, ui::Pt size) {
    return AtomicCommand{CmdResizeNode{id, pos, size}};
}
inline Command cmd_set_routing_points(ui::InternedId id, std::vector<ui::Pt> pts) {
    return AtomicCommand{CmdSetRoutingPoints{id, std::move(pts)}};
}
inline Command cmd_set_grid_step(float step) { return AtomicCommand{CmdSetGridStep{step}}; }
inline Command cmd_set_name(ui::InternedId id, std::string name) { 
    return AtomicCommand{CmdSetName{id, std::move(name)}}; 
}
inline Command cmd_swap_bus_ports(ui::InternedId bus_id, ui::InternedId wire_a, ui::InternedId wire_b) {
    return AtomicCommand{CmdSwapBusPorts{bus_id, wire_a, wire_b}};
}

// Compound command factory
inline Command cmd_compound(std::vector<Command> cmds) {
    std::vector<std::any> any_cmds;
    any_cmds.reserve(cmds.size());
    for (auto& c : cmds) {
        any_cmds.push_back(std::move(c));
    }
    return CmdCompound{std::move(any_cmds)};
}

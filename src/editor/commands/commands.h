#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "ui/core/interned_id.h"
#include <variant>
#include <string>
#include <vector>
#include <utility>

// =============================================================================
// Commands — mutations on bp2::EditorModel
// =============================================================================

struct CmdAddNode       { bp2::Blueprint::Node node; };
struct CmdRemoveNode    {
    ui::InternedId node_id;
    std::vector<ui::InternedId> connected_wire_ids;
};
struct CmdMoveNode      { ui::InternedId node_id; float x; float y; };
struct CmdAddWire       { bp2::Blueprint::Wire wire; };
struct CmdRemoveWire    { ui::InternedId wire_id; };
struct CmdSetParam      { ui::InternedId node_id; ui::InternedId key; float value; };
struct CmdResizeNode    { ui::InternedId node_id; float x; float y; float w; float h; };
struct CmdSetName       { ui::InternedId node_id; std::string new_name; };
struct CmdSetRoutingPoints {
    ui::InternedId wire_id;
    std::vector<std::pair<float,float>> points;
};
struct CmdSetPortLayout {
    ui::InternedId node_id;
    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
};

using Command = std::variant<
    CmdAddNode, CmdRemoveNode, CmdMoveNode,
    CmdAddWire, CmdRemoveWire,
    CmdSetParam, CmdResizeNode, CmdSetName,
    CmdSetRoutingPoints, CmdSetPortLayout
>;

/// Execute a command (takes by value for move semantics). Call push_checkpoint() before.
void execute(bp2::EditorModel& model, ui::StringInterner& interner, Command cmd);

// Factories
inline Command cmd_add_node(bp2::Blueprint::Node n)    { return CmdAddNode{std::move(n)}; }
inline Command cmd_remove_node(ui::InternedId id,
                               std::vector<ui::InternedId> connected_wire_ids) {
    return CmdRemoveNode{id, std::move(connected_wire_ids)};
}
inline Command cmd_move_node(ui::InternedId id, float x, float y) { return CmdMoveNode{id,x,y}; }
inline Command cmd_add_wire(bp2::Blueprint::Wire w)    { return CmdAddWire{std::move(w)}; }
inline Command cmd_remove_wire(ui::InternedId id)       { return CmdRemoveWire{id}; }
inline Command cmd_set_name(ui::InternedId id, std::string nm) { return CmdSetName{id,std::move(nm)}; }
inline Command cmd_resize_node(ui::InternedId id, float x, float y, float w, float h) {
    return CmdResizeNode{id, x, y, w, h};
}
inline Command cmd_set_param(ui::InternedId node_id, ui::InternedId key, float value) {
    return CmdSetParam{node_id, key, value};
}
inline Command cmd_set_routing_points(ui::InternedId id, std::vector<std::pair<float,float>> pts) {
    return CmdSetRoutingPoints{id, std::move(pts)};
}
inline Command cmd_set_port_layout(ui::InternedId node_id,
                                   std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides) {
    return CmdSetPortLayout{node_id, std::move(overrides)};
}

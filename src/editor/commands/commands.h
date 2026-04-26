#pragma once

#include "blueprint_v2/blueprint/blueprint.h"
#include "blueprint_v2/editor_model/editor_model.h"
#include "blueprint_v2/path/path.h"
#include "core/strings/interned_id.h"
#include <variant>
#include <string>
#include <vector>
#include <utility>

// =============================================================================
// Commands — mutations on bp2::EditorModel
// =============================================================================

struct CmdAddNode       { bp2::Blueprint::Node node; };
struct CmdRemoveNode    {
    core::InternedId node_id;
    std::vector<core::InternedId> connected_wire_ids;
};
struct CmdMoveNode      { core::InternedId node_id; float x; float y; };
struct CmdAddWire       { bp2::Blueprint::Wire wire; };
struct CmdRemoveWire    { core::InternedId wire_id; };
struct CmdSetParam      { core::InternedId node_id; core::InternedId key; float value; };
struct CmdResizeNode    { core::InternedId node_id; float x; float y; float w; float h; };
struct CmdSetName       { core::InternedId node_id; std::string new_name; };
struct CmdSetRoutingPoints {
    core::InternedId wire_id;
    std::vector<std::pair<float,float>> points;
};
struct CmdSetPortLayout {
    core::InternedId node_id;
    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
};

using Command = std::variant<
    CmdAddNode, CmdRemoveNode, CmdMoveNode,
    CmdAddWire, CmdRemoveWire,
    CmdSetParam, CmdResizeNode, CmdSetName,
    CmdSetRoutingPoints, CmdSetPortLayout
>;

/// Execute a command (takes by value for move semantics). Call push_checkpoint() before.
void execute(bp2::EditorModel& model, core::StringInterner& interner, Command cmd);

// Factories
inline Command cmd_add_node(bp2::Blueprint::Node n)    { return CmdAddNode{std::move(n)}; }
inline Command cmd_remove_node(core::InternedId id,
                               std::vector<core::InternedId> connected_wire_ids) {
    return CmdRemoveNode{id, std::move(connected_wire_ids)};
}
inline Command cmd_move_node(core::InternedId id, float x, float y) { return CmdMoveNode{id,x,y}; }
inline Command cmd_add_wire(bp2::Blueprint::Wire w)    { return CmdAddWire{std::move(w)}; }
inline Command cmd_remove_wire(core::InternedId id)       { return CmdRemoveWire{id}; }
inline Command cmd_set_name(core::InternedId id, std::string nm) { return CmdSetName{id,std::move(nm)}; }
inline Command cmd_resize_node(core::InternedId id, float x, float y, float w, float h) {
    return CmdResizeNode{id, x, y, w, h};
}
inline Command cmd_set_param(core::InternedId node_id, core::InternedId key, float value) {
    return CmdSetParam{node_id, key, value};
}
inline Command cmd_set_routing_points(core::InternedId id, std::vector<std::pair<float,float>> pts) {
    return CmdSetRoutingPoints{id, std::move(pts)};
}
inline Command cmd_set_port_layout(core::InternedId node_id,
                                   std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides) {
    return CmdSetPortLayout{node_id, std::move(overrides)};
}

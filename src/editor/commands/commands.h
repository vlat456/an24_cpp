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
struct CmdRemoveNode    { ui::InternedId node_id; };
struct CmdMoveNode      { ui::InternedId node_id; float x; float y; };
struct CmdAddWire       { bp2::Blueprint::Wire wire; };
struct CmdRemoveWire    { ui::InternedId wire_id; };
struct CmdSetParam      { ui::InternedId node_id; ui::InternedId key; float value; };
struct CmdResizeNode    { ui::InternedId node_id; float x; float y; float w; float h; };
struct CmdSetGridStep   { float new_step; };
struct CmdSetName       { ui::InternedId node_id; std::string new_name; };
struct CmdAddNested     { bp2::Blueprint::Nested nested; };
struct CmdRemoveNested  { ui::InternedId nested_id; };
struct CmdSetRoutingPoints {
    ui::InternedId wire_id;
    std::vector<std::pair<float,float>> points;
};
struct CmdSetPortLayout {
    ui::InternedId node_id;
    std::vector<bp2::Blueprint::Node::PortLayoutOverride> overrides;
};
struct CmdSetColor {
    ui::InternedId node_id;
    bool has_color;
    float r, g, b, a;
};

using Command = std::variant<
    CmdAddNode, CmdRemoveNode, CmdMoveNode,
    CmdAddWire, CmdRemoveWire,
    CmdSetParam, CmdResizeNode, CmdSetGridStep, CmdSetName,
    CmdAddNested, CmdRemoveNested, CmdSetRoutingPoints, CmdSetPortLayout,
    CmdSetColor
>;

/// Execute a command (takes by value for move semantics). Call push_checkpoint() before.
void execute(bp2::EditorModel& model, ui::StringInterner& interner, Command cmd);

// Factories
inline Command cmd_add_node(bp2::Blueprint::Node n)    { return CmdAddNode{std::move(n)}; }
inline Command cmd_remove_node(ui::InternedId id)       { return CmdRemoveNode{id}; }
inline Command cmd_move_node(ui::InternedId id, float x, float y) { return CmdMoveNode{id,x,y}; }
inline Command cmd_add_wire(bp2::Blueprint::Wire w)    { return CmdAddWire{std::move(w)}; }
inline Command cmd_remove_wire(ui::InternedId id)       { return CmdRemoveWire{id}; }
inline Command cmd_set_grid_step(float s)               { return CmdSetGridStep{s}; }
inline Command cmd_set_name(ui::InternedId id, std::string nm) { return CmdSetName{id,std::move(nm)}; }
inline Command cmd_add_nested(bp2::Blueprint::Nested n){ return CmdAddNested{std::move(n)}; }
inline Command cmd_remove_nested(ui::InternedId id)     { return CmdRemoveNested{id}; }
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
inline Command cmd_set_color(ui::InternedId node_id, bool has_color,
                             float r, float g, float b, float a) {
    return CmdSetColor{node_id, has_color, r, g, b, a};
}

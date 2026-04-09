#pragma once
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "visual/node/text_node_widget.h"
#include "visual/node/group_node_widget.h"
#include "visual/node/bus_node_widget.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include <memory>
#include <vector>

namespace visual {

/// Factory for creating the correct Widget subclass based on node render hint.
struct NodeFactory {
    /// Create a widget for the given node data.
    /// @param node      The node data (render_hint selects the widget type)
    /// @param render_iface The authoritative interface to project for rendering
    /// @param interner  String interner for resolving InternedId to strings
    /// @param wires     All wires in the blueprint (used by BusNodeWidget)
    /// @return Owning pointer to the created widget
    static std::unique_ptr<Widget> create(const bp2::Blueprint::Node& node,
                                          const bp2::Interface& render_iface,
                                          const ui::StringInterner& interner,
                                          const std::vector<BusWireRef>& wires = {}) {
        if (node.view.render_hint == "bus") {
            PortEdge edge = PortEdge::Bottom;
            auto it = node.semantic.string_params.find("port_edge");
            if (it != node.semantic.string_params.end()) {
                if (it->second == "top")         edge = PortEdge::Top;
                else if (it->second == "left")   edge = PortEdge::Left;
                else if (it->second == "right")  edge = PortEdge::Right;
            }
            return std::make_unique<BusNodeWidget>(node, interner, edge, wires);
        }
        if (node.view.render_hint == "ref") {
            return std::make_unique<RefNodeWidget>(node, render_iface, interner);
        }
        if (node.view.render_hint == "group") {
            return std::make_unique<GroupNodeWidget>(node, interner);
        }
        if (node.view.render_hint == "text") {
            return std::make_unique<TextNodeWidget>(node, interner);
        }
        // Default: standard component node
        return std::make_unique<NodeWidget>(node, render_iface, interner);
    }
};

} // namespace visual

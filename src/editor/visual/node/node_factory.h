#pragma once
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "visual/node/text_node_widget.h"
#include "visual/node/group_node_widget.h"
#include "visual/node/bus_node_widget.h"
#include "data/node.h"
#include "data/wire.h"
#include <memory>
#include <vector>

namespace visual {

/// Factory for creating the correct Widget subclass based on Node::render_hint.
struct NodeFactory {
    /// Create a widget for the given node data.
    /// @param node     The node data (render_hint selects the widget type)
    /// @param wires    All wires in the blueprint (used by BusNodeWidget)
    /// @return Owning pointer to the created widget
    static std::unique_ptr<Widget> create(const ::Node& node,
                                           const std::vector<::Wire>& wires = {}) {
        if (node.render_hint == "bus") {
            PortEdge edge = PortEdge::Bottom;
            auto it = node.params.find("port_edge");
            if (it != node.params.end()) {
                if (it->second == "top") edge = PortEdge::Top;
                else if (it->second == "left") edge = PortEdge::Left;
                else if (it->second == "right") edge = PortEdge::Right;
            }
            return std::make_unique<BusNodeWidget>(node, edge, wires);
        }
        if (node.render_hint == "ref") {
            return std::make_unique<RefNodeWidget>(node);
        }
        if (node.render_hint == "group") {
            return std::make_unique<GroupNodeWidget>(node);
        }
        if (node.render_hint == "text") {
            return std::make_unique<TextNodeWidget>(node);
        }
        // Default: standard component node
        return std::make_unique<NodeWidget>(node);
    }
};

} // namespace visual

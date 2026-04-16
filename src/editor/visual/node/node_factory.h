#pragma once
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "visual/node/text_node_widget.h"
#include "visual/node/group_node_widget.h"
#include "visual/node/bus_node_widget.h"
#include "editor/visual/presentation/node_presentation.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "ui/core/interned_id.h"
#include <memory>
#include <vector>

namespace visual {

/// Factory for creating the correct Widget subclass based on NodeFrameKind.
struct NodeFactory {
    /// Create a widget for the given node data.
    /// @param node       The node data
    /// @param frame_kind Resolved frame kind (from TypeDefinition, not view.render_hint)
    /// @param render_iface The authoritative interface to project for rendering
    /// @param interner   String interner for resolving InternedId to strings
    /// @param wires      All wires in the blueprint (used by BusNodeWidget)
    /// @return Owning pointer to the created widget
    static std::unique_ptr<Widget> create(const bp2::Blueprint::Node& node,
                                          editor::presentation::NodeFrameKind frame_kind,
                                          const bp2::Interface& render_iface,
                                          const ui::StringInterner& interner,
                                          const std::vector<BusWireRef>& wires = {}) {
        using editor::presentation::NodeFrameKind;
        switch (frame_kind) {
            case NodeFrameKind::Bus: {
                PortEdge edge = PortEdge::Bottom;
                auto it = node.semantic.string_params.find("port_edge");
                if (it != node.semantic.string_params.end()) {
                    if (it->second == "top")         edge = PortEdge::Top;
                    else if (it->second == "left")   edge = PortEdge::Left;
                    else if (it->second == "right")  edge = PortEdge::Right;
                }
                return std::make_unique<BusNodeWidget>(node, interner, edge, wires);
            }
            case NodeFrameKind::Reference:
                return std::make_unique<RefNodeWidget>(node, render_iface, interner);
            case NodeFrameKind::Group:
                return std::make_unique<GroupNodeWidget>(node, interner);
            case NodeFrameKind::Annotation:
                return std::make_unique<TextNodeWidget>(node, interner);
            case NodeFrameKind::Standard:
            default:
                return std::make_unique<NodeWidget>(node, render_iface, interner);
        }
    }
};

} // namespace visual

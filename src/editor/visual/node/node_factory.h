#pragma once
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "visual/node/text_node_widget.h"
#include "visual/node/group_node_widget.h"
#include "visual/node/bus_node_widget.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include "data/node.h"
#include "data/wire.h"
#include "data/port.h"
#include "ui/core/interned_id.h"
#include <memory>
#include <vector>

namespace visual {

/// Factory for creating the correct Widget subclass based on node render hint.
struct NodeFactory {
    static bp2::Blueprint::Node to_bp2_node(const ::Node& node,
                                            const ui::StringInterner& interner) {
        auto& mut_interner = const_cast<ui::StringInterner&>(interner);
        bp2::Blueprint::Node out;
        out.id = node.id;
        out.type = mut_interner.intern(node.type_name);
        out.name = node.name;
        out.render_hint = node.render_hint;
        out.expandable = node.expandable;
        out.collapsed = node.collapsed;
        out.blueprint_path = node.blueprint_path;
        out.group_id = node.group_id;
        out.x = node.pos.x;
        out.y = node.pos.y;
        if (node.has_explicit_size()) {
            out.width = node.explicit_size().x;
            out.height = node.explicit_size().y;
        }
        out.inputs = node.inputs;
        out.outputs = node.outputs;
        out.content_type = static_cast<bp2::NodeContentType>(node.node_content.type);
        out.content_label = node.node_content.label;
        out.content_value = node.node_content.value;
        out.content_min = node.node_content.min;
        out.content_max = node.node_content.max;
        out.content_unit = node.node_content.unit;
        out.content_state = node.node_content.state;
        out.content_tripped = node.node_content.tripped;
        if (node.color.has_value()) {
            out.has_color = true;
            out.color_r = node.color->r;
            out.color_g = node.color->g;
            out.color_b = node.color->b;
            out.color_a = node.color->a;
        }
        for (const auto& [k, v] : node.params) {
            // Try numeric conversion first; fall back to string_params for
            // non-numeric values like "font_size" = "small" or "text" = "...".
            try {
                out.params[mut_interner.intern(k)] = std::stof(v);
            } catch (const std::invalid_argument&) {
                out.string_params[k] = v;
            } catch (const std::out_of_range&) {
                out.string_params[k] = v;
            }
        }
        for (const auto& lo : node.layout_overrides) {
            bp2::Blueprint::Node::PortLayoutOverride ov;
            ov.port_name = lo.port_name;
            if (lo.side.has_value()) {
                switch (*lo.side) {
                    case PortLayoutSide::Top: ov.side = "top"; break;
                    case PortLayoutSide::Right: ov.side = "right"; break;
                    case PortLayoutSide::Bottom: ov.side = "bottom"; break;
                    case PortLayoutSide::Left: ov.side = "left"; break;
                }
            }
            if (lo.position.has_value()) {
                ov.position = static_cast<int>(*lo.position);
            }
            out.layout_overrides.push_back(std::move(ov));
        }
        return out;
    }

    static std::vector<BusWireRef> to_bus_wire_refs(const std::vector<::Wire>& wires) {
        std::vector<BusWireRef> out;
        out.reserve(wires.size());
        for (const auto& w : wires) {
            out.push_back(BusWireRef{w.id, w.start.node_id, w.end.node_id});
        }
        return out;
    }

    /// Create a widget for the given node data.
    /// @param node      The node data (render_hint selects the widget type)
    /// @param interner  String interner for resolving InternedId to strings
    /// @param wires     All wires in the blueprint (used by BusNodeWidget)
    /// @return Owning pointer to the created widget
    static std::unique_ptr<Widget> create(const bp2::Blueprint::Node& node,
                                            const ui::StringInterner& interner,
                                            const std::vector<BusWireRef>& wires = {}) {
        if (node.render_hint == "bus") {
            PortEdge edge = PortEdge::Bottom;
            return std::make_unique<BusNodeWidget>(node, interner, edge, wires);
        }
        if (node.render_hint == "ref") {
            return std::make_unique<RefNodeWidget>(node, interner);
        }
        if (node.render_hint == "group") {
            return std::make_unique<GroupNodeWidget>(node, interner);
        }
        if (node.render_hint == "text") {
            return std::make_unique<TextNodeWidget>(node, interner);
        }
        // Default: standard component node
        return std::make_unique<NodeWidget>(node, interner);
    }

    static std::unique_ptr<Widget> create(const ::Node& node,
                                           const ui::StringInterner& interner,
                                           const std::vector<::Wire>& wires = {}) {
        return create(to_bp2_node(node, interner), interner, to_bus_wire_refs(wires));
    }
};

} // namespace visual

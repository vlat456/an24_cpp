#pragma once
#include "ui/renderer/render_context.h"
#include <vector>
#include <unordered_set>
#include <string_view>
#include <cstddef>

namespace visual {

using ui::Pt;

class Widget;
class Wire;

/// Hash for string_view in unordered containers.
struct StringViewHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

/// Semantic identifier for a hovered routing point.
/// Decouples the render context from widget pointers — the renderer
/// compares wire-id + child index instead of RoutingPoint*.
struct HoveredRoutingPointId {
    std::string_view wire_id;   ///< Wire owning the routing point (empty = none).
    size_t index = 0;           ///< Child index within the wire's children().
    bool empty() const { return wire_id.empty(); }
};

/// Bundles all state needed for a single render frame.
/// Passed through the widget tree so every render() can transform
/// world coordinates to screen coordinates and query selection/hover.
struct RenderContext : public ui::RenderContext {
    const std::vector<Widget*>* selected_nodes = nullptr;
    const Wire* selected_wire = nullptr;
    const Wire* hovered_wire = nullptr;
    HoveredRoutingPointId hovered_routing_point;
    bool show_debug_bounds = false;
    bool show_debug_paint_bounds = false;

    /// Set of visual wire IDs that are energized (voltage > threshold).
    /// Populated per frame from simulation state. nullptr when simulation is off.
    /// string_view keys reference the StringInterner's stable deque storage.
    const std::unordered_set<std::string_view, StringViewHash>* energized_wires = nullptr;

    /// Check whether a widget pointer is in the selected_nodes list.
    bool isNodeSelected(const Widget* w) const {
        if (!selected_nodes) return false;
        for (const auto* s : *selected_nodes) {
            if (s == w) return true;
        }
        return false;
    }
};

} // namespace visual

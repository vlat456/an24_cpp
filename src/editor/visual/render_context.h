#pragma once
#include "ui/math/pt.h"
#include <vector>
#include <unordered_set>
#include <string>

namespace visual {

using ui::Pt;

class Widget;
class Wire;
class RoutingPoint;

/// Bundles all state needed for a single render frame.
/// Passed through the widget tree so every render() can transform
/// world coordinates to screen coordinates and query selection/hover.
struct RenderContext {
    float zoom = 1.0f;
    Pt pan{0, 0};
    Pt canvas_min{0, 0};

    const std::vector<Widget*>* selected_nodes = nullptr;
    const Wire* selected_wire = nullptr;
    const Wire* hovered_wire = nullptr;
    const RoutingPoint* hovered_routing_point = nullptr;

    /// Set of visual wire IDs that are energized (voltage > threshold).
    /// Populated per frame from simulation state. nullptr when simulation is off.
    const std::unordered_set<std::string>* energized_wires = nullptr;

    /// Transform a world-space point to screen-space.
    /// screen = (world - pan) * zoom + canvas_min
    Pt world_to_screen(Pt world) const {
        return Pt((world.x - pan.x) * zoom + canvas_min.x,
                  (world.y - pan.y) * zoom + canvas_min.y);
    }

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

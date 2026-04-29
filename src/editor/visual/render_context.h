#pragma once
#include "core/strings/interned_id.h"
#include "ui/renderer/render_context.h"
#include "visual/string_view_hash.h"
#include <vector>
#include <unordered_set>
#include <string_view>
#include <cstddef>

namespace visual {

using ui::Pt;

class Widget;
class Wire;
class NodeSpriteCache;

/// Semantic identifier for a hovered routing point.
/// Decouples the render context from widget pointers — the renderer
/// compares wire InternedId + child index instead of RoutingPoint*.
struct HoveredRoutingPointId {
    core::InternedId wire_iid;     ///< Interned wire identity (empty = none).
    size_t index = 0;            ///< Child index within the wire's children().
    bool empty() const { return wire_iid.empty(); }
};

/// Bundles all state needed for a single render frame.
/// Passed through the widget tree so every render() can transform
/// world coordinates to screen coordinates and query selection/hover.
struct RenderContext : public ui::RenderContext {
    const std::vector<std::string_view>* selected_node_ids = nullptr;
    std::string_view selected_wire_id;
    std::string_view hovered_wire_id;
    HoveredRoutingPointId hovered_routing_point;
    bool show_debug_bounds = false;
    bool show_debug_paint_bounds = false;

    /// Pre-rendered port circle texture (editor builds only, null in tests).
    /// When non-null, use AddImage instead of AddCircleFilled for port circles.
    void* port_circle_texture = nullptr;

    /// Set of visual wire IDs that are energized (voltage > threshold).
    /// Populated per frame from simulation state. nullptr when simulation is off.
    /// string_view keys reference the StringInterner's stable deque storage.
    const std::unordered_set<std::string_view, StringViewHash>* energized_wires = nullptr;

    /// Node sprite cache — when non-null, Scene::render will blit cached nodes
    /// instead of calling renderTree(). Editor builds only.
    NodeSpriteCache* sprite_cache = nullptr;

    /// Check whether a node id is selected.
    bool isNodeSelected(std::string_view node_id) const {
        if (!selected_node_ids || node_id.empty()) return false;
        for (const auto& id : *selected_node_ids) {
            if (id == node_id) return true;
        }
        return false;
    }
};

} // namespace visual

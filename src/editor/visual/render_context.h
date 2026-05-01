#pragma once
#include "core/strings/interned_id.h"
#include "ui/renderer/render_context.h"
#include "visual/string_view_hash.h"
#include <functional>
#include <vector>
#include <unordered_set>
#include <string_view>
#include <cstddef>

namespace visual {

using ui::Pt;

class Widget;
class Wire;
class ISpriteCache;

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
    /// Selected node IDs — O(1) lookup via unordered_set.
    /// nullptr when no nodes are selected.
    const std::unordered_set<std::string_view, StringViewHash>* selected_node_ids = nullptr;
    std::string_view selected_wire_id;
    std::string_view hovered_wire_id;
    HoveredRoutingPointId hovered_routing_point;
    bool show_debug_bounds = false;
    bool show_debug_paint_bounds = false;

    /// Pre-rendered port circle texture (editor builds only, 0 in tests).
    /// When non-zero, use add_image instead of add_circle_filled for port circles.
    ui::IDrawList::NativeTexture port_circle_texture = 0;

    /// Set of visual wire IDs that are energized (voltage > threshold).
    /// Populated per frame from simulation state. nullptr when simulation is off.
    /// string_view keys reference the StringInterner's stable deque storage.
    const std::unordered_set<std::string_view, StringViewHash>* energized_wires = nullptr;

    /// Node sprite cache — when non-null, Scene::render will blit cached nodes
    /// instead of calling renderTree(). Null in test builds.
    ISpriteCache* sprite_cache = nullptr;

    /// Predicate: returns true if a node should bypass the sprite cache this frame.
    /// When set, bake_dirty_nodes() skips the node (no wasted GPU bake) and
    /// Scene::render() calls renderTree() live for crisp interactive feedback.
    /// Built from transient input state — captures by value (16-byte string_view
    /// fits in SBO, zero heap allocation). No lifecycle management needed.
    /// nullptr (default) = no bypass.
    std::function<bool(std::string_view node_id)> cache_bypass;

    /// Check whether a node id is selected. O(1) via unordered_set.
    bool isNodeSelected(std::string_view node_id) const {
        if (!selected_node_ids || node_id.empty()) return false;
        return selected_node_ids->count(node_id) > 0;
    }
};

} // namespace visual

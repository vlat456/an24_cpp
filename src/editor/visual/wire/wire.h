#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "router/crossings.h"
#include "ui/core/small_vector.h"
#include <vector>
#include <string_view>

namespace visual {

class RoutingPoint;
class Scene;

/// Endpoint descriptor: identifies a port by node ID + port name.
/// The Wire resolves world positions dynamically via scene_->find(node_id)->portByName(...).
/// Stores string_view references into the StringInterner's stable deque storage.
struct WireEndpoint {
    std::string_view node_id;
    std::string_view port_name;
    std::string_view wire_id;   ///< Passed to portByName() for bus alias port resolution.
};

/// Wire is a root-level widget in the Scene.
/// Stores endpoint IDs and resolves world positions dynamically.
/// Owns RoutingPoint children for polyline bends.
///
/// Bounding box: overrides worldMin()/worldMax() to compute from polyline.
/// Wire's local_pos_ stays at (0,0) — it has no positional meaning.
/// RoutingPoints store absolute world coords in their local_pos_.
///
/// All string_view members reference the StringInterner's stable deque storage.
/// They remain valid for the lifetime of the Blueprint that owns the interner.
class Wire : public Widget {
public:
    Wire(std::string_view id,
         std::string_view start_node, std::string_view start_port,
         std::string_view end_node, std::string_view end_port);
    ~Wire() override = default;

    std::string_view id() const override { return id_; }
    bool isClickable() const override { return true; }
    RenderLayer renderLayer() const override { return RenderLayer::Wire; }

    const WireEndpoint& startEndpoint() const { return start_; }
    const WireEndpoint& endEndpoint() const { return end_; }

    /// Build polyline: start -> routing points -> end (world coords).
    /// Result is cached; use invalidateGeometry() to force recomputation.
    const std::vector<Pt>& polyline() const;

    /// Override bounding box for Grid spatial indexing.
    Pt worldMin() const override;
    Pt worldMax() const override;

    RoutingPoint* addRoutingPoint(Pt pos, size_t index);
    void removeRoutingPoint(size_t index);

    /// Mark cached geometry (polyline + bounding box) as stale.
    /// External callers (e.g. node drag) should call this when wire endpoints move.
    void invalidateGeometry() const { dirty_ = true; }

    void render(IDrawList* dl, const RenderContext& ctx) const override;

    /// Crossing points where other wires cross over this wire.
    /// Populated by compute_wire_crossings() before rendering each frame.
    void clearCrossings() { crossings_.clear(); }
    void appendCrossing(const WireCrossing& c) { crossings_.push_back(c); }
    const ui::SmallVector<WireCrossing, 4>& crossings() const { return crossings_; }

    static constexpr float WIRE_THICKNESS = 1.5f;

private:
    std::string_view id_;
    WireEndpoint start_;
    WireEndpoint end_;
    ui::SmallVector<WireCrossing, 4> crossings_;

    static constexpr float BBOX_PADDING = 4.0f;

    /// Resolve a WireEndpoint to a world position via the scene graph.
    /// Returns the port center (port worldPos + Port::RADIUS offset).
    /// Returns nullopt if the node or port cannot be found.
    std::optional<Pt> resolveEndpoint(const WireEndpoint& ep) const;

    /// Rebuild cached_polyline_, cached_min_, cached_max_ if dirty.
    void rebuildGeometry() const;

    mutable bool dirty_ = true;
    mutable std::vector<Pt> cached_polyline_;
    mutable Pt cached_min_{0, 0};
    mutable Pt cached_max_{0, 0};
    /// Snapshot of endpoint world positions at last rebuild — used to
    /// auto-detect external moves without requiring explicit invalidation.
    mutable Pt cached_start_pos_{0, 0};
    mutable Pt cached_end_pos_{0, 0};
};

/// Compute wire crossings for all wires in a scene.
/// Uses the spatial Grid for broadphase: only wire pairs sharing a
/// Grid cell are tested for segment intersections.
/// Call this before Scene::render() each frame.
void compute_wire_crossings(Scene& scene);

} // namespace visual

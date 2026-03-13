#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "router/crossings.h"
#include "ui/core/small_vector.h"
#include <vector>
#include <string>

namespace visual {

class WireEnd;
class RoutingPoint;
class Scene;

/// Wire is a root-level widget in the Scene.
/// Connects two WireEnds (which live as children of Ports).
/// Owns RoutingPoint children for polyline bends.
///
/// Bounding box: overrides worldMin()/worldMax() to compute from polyline.
/// Wire's local_pos_ stays at (0,0) — it has no positional meaning.
/// RoutingPoints store absolute world coords in their local_pos_.
class Wire : public Widget {
public:
    Wire(const std::string& id, WireEnd* start, WireEnd* end);
    ~Wire() override;

    std::string_view id() const override { return id_; }
    bool isClickable() const override { return true; }
    RenderLayer renderLayer() const override { return RenderLayer::Wire; }

    WireEnd* start() const { return start_; }
    WireEnd* end() const { return end_; }

    /// Build polyline: start -> routing points -> end (world coords).
    /// Result is cached; use invalidateGeometry() to force recomputation.
    const std::vector<Pt>& polyline() const;

    /// Override bounding box for Grid spatial indexing.
    Pt worldMin() const override;
    Pt worldMax() const override;

    RoutingPoint* addRoutingPoint(Pt pos, size_t index);
    void removeRoutingPoint(size_t index);

    /// Mark cached geometry (polyline + bounding box) as stale.
    /// Called automatically on routing-point add/remove and endpoint destruction.
    /// External callers (e.g. node drag) should call this when wire endpoints move.
    void invalidateGeometry() const { dirty_ = true; }

    void render(IDrawList* dl, const RenderContext& ctx) const override;

    /// Called by WireEnd destructor — triggers deferred self-removal
    void onEndpointDestroyed(WireEnd* end);

    /// Silently detach a WireEnd pointer without triggering self-removal.
    /// Used by BusNodeWidget::rebuildPorts() to prevent dangling pointers
    /// when ports are rebuilt. Unlike onEndpointDestroyed(), this does NOT
    /// queue the wire for scene removal.
    void detachEndpoint(WireEnd* end);

    /// Crossing points where other wires cross over this wire.
    /// Populated by compute_wire_crossings() before rendering each frame.
    void clearCrossings() { crossings_.clear(); }
    void appendCrossing(const WireCrossing& c) { crossings_.push_back(c); }
    const ui::SmallVector<WireCrossing, 4>& crossings() const { return crossings_; }

    static constexpr float WIRE_THICKNESS = 1.5f;

private:
    std::string id_;
    WireEnd* start_;
    WireEnd* end_;
    ui::SmallVector<WireCrossing, 4> crossings_;

    static constexpr float BBOX_PADDING = 4.0f;

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

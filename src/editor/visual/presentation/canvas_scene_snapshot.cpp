#include "editor/visual/presentation/canvas_scene_snapshot.h"
#include "editor/visual/presentation/hit_geometry.h"
#include "editor/visual/presentation/semantic_scene_hittest.h"
#include "visual/scene.h"
#include "visual/widget.h"
#include "visual/node/visual_node.h"
#include "visual/node/ref_node_widget.h"
#include "visual/node/group_node_widget.h"
#include "visual/node/bus_node_widget.h"
#include "visual/node/text_node_widget.h"
#include "visual/port/visual_port.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "editor/layout_constants.h"
#include "ui/core/interned_id.h"
#include <optional>
#include <algorithm>
#include <cmath>

namespace editor::presentation {

namespace {

ui::Rect point_to_rect(ui::Pt pt, float radius) {
    return ui::Rect{
        .x = pt.x - radius,
        .y = pt.y - radius,
        .w = radius * 2.0f,
        .h = radius * 2.0f
    };
}

ui::Rect widget_bounds(const visual::Widget& widget) {
    const auto min = widget.worldMin();
    const auto max = widget.worldMax();
    return ui::Rect{min.x, min.y, max.x - min.x, max.y - min.y};
}

ui::Rect offset_rect(const ui::Rect& r, ui::Pt offset) {
    return ui::Rect{r.x + offset.x, r.y + offset.y, r.w, r.h};
}

/// Helper: extract node ID from any node-like widget.
/// Returns the node_iid by interning the widget's id() (which equals nodeId() for all node widgets).
ui::InternedId node_widget_iid(const visual::Widget& widget, ui::StringInterner& interner) {
    return interner.intern(widget.id());
}

/// True if `widget` is a node-like widget (clickable, not a wire/port/routing point).
/// Matches the same semantics as the old grid-based hit_test_node_body.
bool is_node_like(const visual::Widget& widget) {
    if (!widget.isClickable()) return false;
    if (dynamic_cast<const visual::Wire*>(&widget)) return false;
    if (dynamic_cast<const visual::Port*>(&widget)) return false;
    if (dynamic_cast<const visual::RoutingPoint*>(&widget)) return false;
    return true;
}

void project_widget_recursive(const visual::Widget& widget,
                             CanvasSceneSnapshot& snapshot,
                             ui::StringInterner& interner,
                             uint32_t& next_id) {
    // ---- Ports ----
    if (auto* port = dynamic_cast<const visual::Port*>(&widget)) {
        const ui::Pt center = port->worldPos() + ui::Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
        const ui::InternedId port_node_iid = interner.intern(port->rootAncestorId());
        const ui::InternedId port_name_iid = interner.intern(port->name());

        snapshot.render_objects.push_back(CanvasRenderObject{
            .id = SceneObjectId(next_id++),
            .node_id = port_node_iid,
            .element_id = port_name_iid,
            .kind = CanvasRenderObjectKind::Port,
            .bounds = point_to_rect(center, visual::PortConstants::RADIUS),
            .fill_color = port->color(),
        });

        CanvasHitObject ph{};
        ph.id = SceneObjectId(next_id++);
        ph.node_id = port_node_iid;
        ph.element_id = port_name_iid;
        ph.kind = CanvasHitObjectKind::Port;
        ph.shape = HitShapeKind::Circle;
        ph.bounds = point_to_rect(center, visual::PortConstants::HIT_RADIUS);
        ph.port_direction = port->direction();
        ph.port_type = port->type();
        snapshot.hit_objects.push_back(std::move(ph));
    }
    // ---- Wires ----
    else if (auto* wire = dynamic_cast<const visual::Wire*>(&widget)) {
        const auto& polyline = wire->polyline();
        if (polyline.size() >= 2) {
            float min_x = polyline[0].x, max_x = polyline[0].x;
            float min_y = polyline[0].y, max_y = polyline[0].y;
            for (const auto& pt : polyline) {
                min_x = std::min(min_x, pt.x);
                max_x = std::max(max_x, pt.x);
                min_y = std::min(min_y, pt.y);
                max_y = std::max(max_y, pt.y);
            }
            constexpr float BBOX_PADDING = 4.0f;
            const ui::InternedId wire_iid = wire->iid();

            snapshot.render_objects.push_back(CanvasRenderObject{
                .id = SceneObjectId(next_id++),
                .node_id = {},
                .element_id = wire_iid,
                .kind = CanvasRenderObjectKind::Wire,
                .bounds = ui::Rect{min_x - BBOX_PADDING, min_y - BBOX_PADDING,
                                   max_x - min_x + 2.0f * BBOX_PADDING,
                                   max_y - min_y + 2.0f * BBOX_PADDING},
                .stroke_width = visual::Wire::WIRE_THICKNESS,
            });

            for (size_t i = 0; i + 1 < polyline.size(); ++i) {
                const auto& p0 = polyline[i];
                const auto& p1 = polyline[i + 1];

                CanvasHitObject wh{};
                wh.id = SceneObjectId(next_id++);
                wh.element_id = wire_iid;
                wh.kind = CanvasHitObjectKind::WireSegment;
                wh.shape = HitShapeKind::Rectangle;
                wh.bounds = ui::Rect{std::min(p0.x, p1.x) - hit_geometry::wire_segment_hit_tolerance(),
                                     std::min(p0.y, p1.y) - hit_geometry::wire_segment_hit_tolerance(),
                                     std::max(p0.x, p1.x) - std::min(p0.x, p1.x) + 2.0f * hit_geometry::wire_segment_hit_tolerance(),
                                     std::max(p0.y, p1.y) - std::min(p0.y, p1.y) + 2.0f * hit_geometry::wire_segment_hit_tolerance()};
                wh.segment_index = i;
                wh.segment_p0 = p0;
                wh.segment_p1 = p1;
                snapshot.hit_objects.push_back(std::move(wh));
            }
        }
    }
    // ---- Routing points ----
    else if (auto* rp = dynamic_cast<const visual::RoutingPoint*>(&widget)) {
        const ui::Pt rp_pos = rp->worldPos();
        const ui::Rect rp_render_bounds = hit_geometry::centered_square(rp_pos, hit_geometry::routing_point_render_radius());
        const ui::Rect rp_hit_bounds = hit_geometry::centered_square(rp_pos, hit_geometry::routing_point_hit_radius());

        // Find the owning Wire and compute routing-point index
        ui::InternedId wire_iid;
        size_t rp_idx = 0;
        if (auto* parent_widget = rp->parent()) {
            if (auto* wire = dynamic_cast<const visual::Wire*>(parent_widget)) {
                wire_iid = wire->iid();
                for (size_t i = 0; i < wire->children().size(); ++i) {
                    if (wire->children()[i].get() == rp) { rp_idx = i; break; }
                }
            }
        }
        const ui::InternedId rp_node_iid = interner.intern(rp->rootAncestorId());

        snapshot.render_objects.push_back(CanvasRenderObject{
            .id = SceneObjectId(next_id++),
            .node_id = rp_node_iid,
            .element_id = {},
            .kind = CanvasRenderObjectKind::RoutingPoint,
            .bounds = rp_render_bounds,
            .fill_color = 0xFF888888,
        });

        CanvasHitObject rph{};
        rph.id = SceneObjectId(next_id++);
        rph.node_id = rp_node_iid;
        rph.kind = CanvasHitObjectKind::RoutingPoint;
        rph.shape = HitShapeKind::Rectangle;
        rph.bounds = rp_hit_bounds;
        rph.rp_wire_id = wire_iid;
        rph.rp_index = rp_idx;
        snapshot.hit_objects.push_back(std::move(rph));
    }
    // ---- Node-like widgets (NodeWidget, RefNodeWidget, BusNodeWidget, TextNodeWidget, GroupNodeWidget) ----
    else if (is_node_like(widget)) {
        ui::InternedId node_iid = node_widget_iid(widget, interner);
        const ui::Rect node_bounds = widget_bounds(widget);

        // Only NodeWidget has semantic content rendering
        bool has_semantic = false;
        SemanticSceneSnapshot content_ss;
        Bounds cb{};
        std::optional<InteractionBinding> content_interaction;
        bool is_group = false;

        if (auto* node = dynamic_cast<const visual::NodeWidget*>(&widget)) {
            has_semantic = node->renders_content_from_semantic_snapshot();
            content_ss = node->content_semantic_snapshot();
            cb = node->contentBounds();

            if (has_semantic && !content_ss.hit_objects.empty()) {
                for (const auto& hit_obj : content_ss.hit_objects) {
                    if (!hit_obj.interactions.empty()) {
                        content_interaction = hit_obj.interactions.front();
                        break;
                    }
                }
            }
        }

        if (dynamic_cast<const visual::GroupNodeWidget*>(&widget)) {
            is_group = true;
        }

        CanvasHitObject node_hit{};
        node_hit.id = SceneObjectId(next_id++);
        node_hit.node_id = node_iid;
        node_hit.kind = CanvasHitObjectKind::NodeBody;
        node_hit.shape = HitShapeKind::Rectangle;
        node_hit.bounds = node_bounds;
        node_hit.node_world_pos = widget.worldPos();
        node_hit.node_size = widget.size();
        node_hit.content_bounds = cb;
        node_hit.content_snapshot = content_ss;
        node_hit.renders_content_from_semantic_snapshot = has_semantic;
        node_hit.content_interaction = content_interaction;
        node_hit.is_group = is_group;
        snapshot.hit_objects.push_back(std::move(node_hit));

        snapshot.render_objects.push_back(CanvasRenderObject{
            .id = SceneObjectId(next_id++),
            .node_id = node_iid,
            .element_id = {},
            .kind = CanvasRenderObjectKind::NodeBody,
            .bounds = node_bounds,
        });

        if (has_semantic) {
            const ui::Pt content_offset(widget.worldPos().x + cb.x,
                                        widget.worldPos().y + cb.y);

            for (const auto& sem_obj : content_ss.render_objects) {
                CanvasRenderObjectKind kind = CanvasRenderObjectKind::ContentPaint;
                switch (sem_obj.kind) {
                    case SceneRenderObjectKind::NodeFrame:
                        kind = CanvasRenderObjectKind::NodeFrame;
                        break;
                    case SceneRenderObjectKind::NodeTitle:
                        kind = CanvasRenderObjectKind::NodeTitle;
                        break;
                    case SceneRenderObjectKind::NodeFooter:
                        kind = CanvasRenderObjectKind::NodeFooter;
                        break;
                    case SceneRenderObjectKind::ContentPaint:
                        kind = CanvasRenderObjectKind::ContentPaint;
                        break;
                }

                snapshot.render_objects.push_back(CanvasRenderObject{
                    .id = SceneObjectId(next_id++),
                    .node_id = node_iid,
                    .element_id = sem_obj.element_id,
                    .kind = kind,
                    .geometry = sem_obj.geometry,
                    .bounds = offset_rect(sem_obj.bounds, content_offset),
                    .text = sem_obj.text,
                    .fill_color = sem_obj.fill_color,
                    .stroke_color = sem_obj.stroke_color,
                    .stroke_width = sem_obj.stroke_width,
                });
            }

            for (const auto& sem_hit : content_ss.hit_objects) {
                CanvasHitObject content_hit{};
                content_hit.id = SceneObjectId(next_id++);
                content_hit.node_id = node_iid;
                content_hit.element_id = sem_hit.element_id;
                content_hit.kind = CanvasHitObjectKind::ContentRegion;
                content_hit.shape = sem_hit.shape;
                content_hit.bounds = offset_rect(sem_hit.bounds, content_offset);
                snapshot.hit_objects.push_back(std::move(content_hit));
            }
        }

        if (widget.isResizable()) {
            const auto min_pt = widget.worldMin();
            const auto max_pt = widget.worldMax();
            const ui::Pt nwp = widget.worldPos();
            const ui::Pt nsz = widget.size();

            struct { ui::Pt center; ResizeCorner corner; } handles[] = {
                {min_pt, ResizeCorner::TopLeft},
                {ui::Pt(max_pt.x, min_pt.y), ResizeCorner::TopRight},
                {ui::Pt(min_pt.x, max_pt.y), ResizeCorner::BottomLeft},
                {max_pt, ResizeCorner::BottomRight},
            };
            for (const auto& h : handles) {
                CanvasHitObject rh{};
                rh.id = SceneObjectId(next_id++);
                rh.node_id = node_iid;
                rh.kind = CanvasHitObjectKind::ResizeHandle;
                rh.shape = HitShapeKind::Rectangle;
                rh.bounds = hit_geometry::resize_handle_hit_bounds(h.center, h.corner);
                rh.corner = h.corner;
                rh.node_world_pos = nwp;
                rh.node_size = nsz;
                snapshot.hit_objects.push_back(std::move(rh));
            }
        }
    }

    for (const auto& child : widget.children()) {
        if (auto* visual_child = dynamic_cast<const visual::Widget*>(child.get())) {
            project_widget_recursive(*visual_child, snapshot, interner, next_id);
        }
    }
}

} // namespace

// ============================================================
// Build snapshot
// ============================================================

CanvasSceneSnapshot build_canvas_scene_snapshot(const visual::Scene& scene, ui::StringInterner& interner) {
    CanvasSceneSnapshot snapshot;
    uint32_t next_id = 1;

    for (const auto& root_ptr : scene.roots()) {
        auto* widget = dynamic_cast<visual::Widget*>(root_ptr.get());
        if (!widget) continue;
        project_widget_recursive(*widget, snapshot, interner, next_id);
    }

    return snapshot;
}

// ============================================================
// Snapshot-based hit testing
// ============================================================

visual::HitResult hit_test_canvas_scene(const CanvasSceneSnapshot& snapshot, ui::Pt world_pos) {
    // We scan all hit objects and track best match per priority level.
    // Priority (descending): Port > RoutingPoint > ResizeHandle > NodeBody > WireSegment.

    const CanvasHitObject* best_port = nullptr;
    const CanvasHitObject* best_routing_point = nullptr;
    const CanvasHitObject* best_resize_handle = nullptr;
    const CanvasHitObject* best_node = nullptr;
    const CanvasHitObject* best_wire = nullptr;
    float best_wire_dist = 1e9f;

    for (const auto& obj : snapshot.hit_objects) {
        switch (obj.kind) {
            case CanvasHitObjectKind::Port: {
                float cx = obj.bounds.x + obj.bounds.w * 0.5f;
                float cy = obj.bounds.y + obj.bounds.h * 0.5f;
                ui::Pt center(cx, cy);
                if (hit_geometry::distance(world_pos, center) <= hit_geometry::port_hit_radius()) {
                    best_port = &obj;
                }
                break;
            }
            case CanvasHitObjectKind::RoutingPoint: {
                float cx = obj.bounds.x + obj.bounds.w * 0.5f;
                float cy = obj.bounds.y + obj.bounds.h * 0.5f;
                ui::Pt center(cx, cy);
                if (hit_geometry::distance(world_pos, center) <= hit_geometry::routing_point_hit_radius()) {
                    best_routing_point = &obj;
                }
                break;
            }
            case CanvasHitObjectKind::ResizeHandle: {
                if (hit_geometry::point_in_rect(world_pos, obj.bounds)) {
                    best_resize_handle = &obj;
                }
                break;
            }
            case CanvasHitObjectKind::NodeBody: {
                if (obj.is_group) {
                    if (hit_geometry::point_hits_group_frame(world_pos, obj.bounds)) {
                        best_node = &obj;
                    }
                } else if (hit_geometry::point_in_rect(world_pos, obj.bounds)) {
                    // If multiple nodes overlap, prefer the last one (higher render layer).
                    best_node = &obj;
                }
                break;
            }
            case CanvasHitObjectKind::WireSegment: {
                float d = hit_geometry::distance_to_segment(world_pos, obj.segment_p0, obj.segment_p1);
                if (d < hit_geometry::wire_segment_hit_tolerance() && d < best_wire_dist) {
                    best_wire_dist = d;
                    best_wire = &obj;
                }
                break;
            }
            case CanvasHitObjectKind::ContentRegion:
                // Content regions are hit-tested as part of NodeBody below.
                break;
        }
    }

    // --- Priority 1: Ports ---
    if (best_port) {
        float cx = best_port->bounds.x + best_port->bounds.w * 0.5f;
        float cy = best_port->bounds.y + best_port->bounds.h * 0.5f;
        return visual::HitPort{
            .node_id = best_port->node_id,
            .port_name = best_port->element_id,
            .direction = best_port->port_direction,
            .type = best_port->port_type,
            .center = ui::Pt(cx, cy),
        };
    }

    // --- Priority 2: Routing points ---
    if (best_routing_point) {
        float cx = best_routing_point->bounds.x + best_routing_point->bounds.w * 0.5f;
        float cy = best_routing_point->bounds.y + best_routing_point->bounds.h * 0.5f;
        return visual::HitRoutingPoint{
            .wire_id = best_routing_point->rp_wire_id,
            .index = best_routing_point->rp_index,
            .world_pos = ui::Pt(cx, cy),
        };
    }

    // --- Priority 3: Resize handles ---
    if (best_resize_handle) {
        return visual::HitResizeHandle{
            .node_id = best_resize_handle->node_id,
            .corner = best_resize_handle->corner,
            .world_pos = best_resize_handle->node_world_pos,
            .size = best_resize_handle->node_size,
        };
    }

    // --- Priority 4: Nodes ---
    if (best_node) {
        visual::HitNode hit{};
        hit.node_id = best_node->node_id;
        hit.world_pos = best_node->node_world_pos;
        hit.size = best_node->node_size;
        hit.content_bounds = best_node->content_bounds;
        hit.content_snapshot = best_node->content_snapshot;
        hit.renders_content_from_semantic_snapshot = best_node->renders_content_from_semantic_snapshot;

        // Check for content interaction using semantic scene hit test
        if (hit.renders_content_from_semantic_snapshot && !hit.content_snapshot.hit_objects.empty()) {
            const ui::Pt local(world_pos.x - hit.world_pos.x,
                               world_pos.y - hit.world_pos.y);
            auto semantic_hit = hit_test_semantic_scene(hit.content_snapshot, local);
            if (auto* content_region = std::get_if<SemanticHitContentRegion>(&semantic_hit)) {
                if (content_region->object && !content_region->object->interactions.empty()) {
                    const auto& binding = content_region->object->interactions.front();
                    visual::HitContentInteraction interaction;
                    interaction.kind = binding.kind;
                    interaction.primary_min = binding.min_value;
                    interaction.primary_max = binding.max_value;
                    interaction.steps = static_cast<int>(binding.step);
                    if (interaction.steps < 2) interaction.steps = 2;
                    hit.content_interaction = interaction;
                }
            }
        }

        return hit;
    }

    // --- Priority 5: Wire segments ---
    if (best_wire) {
        return visual::HitWire{
            .wire_id = best_wire->element_id,
            .segment = best_wire->segment_index,
        };
    }

    return visual::HitEmpty{};
}

visual::HitResult hit_test_canvas_scene_ports(const CanvasSceneSnapshot& snapshot, ui::Pt world_pos) {
    for (const auto& obj : snapshot.hit_objects) {
        if (obj.kind != CanvasHitObjectKind::Port) continue;

        float cx = obj.bounds.x + obj.bounds.w * 0.5f;
        float cy = obj.bounds.y + obj.bounds.h * 0.5f;
        ui::Pt center(cx, cy);
        if (hit_geometry::distance(world_pos, center) <= hit_geometry::port_hit_radius()) {
            return visual::HitPort{
                .node_id = obj.node_id,
                .port_name = obj.element_id,
                .direction = obj.port_direction,
                .type = obj.port_type,
                .center = center,
            };
        }
    }

    return visual::HitEmpty{};
}

} // namespace editor::presentation

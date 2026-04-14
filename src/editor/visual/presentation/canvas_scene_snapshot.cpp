#include "editor/visual/presentation/canvas_scene_snapshot.h"
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

/// Convert ui::Pt with radius to a Rect.
Rect point_to_rect(ui::Pt pt, float radius) {
    return Rect{
        .x = pt.x - radius,
        .y = pt.y - radius,
        .w = radius * 2.0f,
        .h = radius * 2.0f
    };
}

Rect widget_bounds(const visual::Widget& widget) {
    const auto min = widget.worldMin();
    const auto max = widget.worldMax();
    return Rect{min.x, min.y, max.x - min.x, max.y - min.y};
}

Rect offset_rect(const Rect& r, ui::Pt offset) {
    return Rect{r.x + offset.x, r.y + offset.y, r.w, r.h};
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
        ph.port_side = port->side();
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
            const ui::InternedId wire_iid = interner.intern(wire->id());

            snapshot.render_objects.push_back(CanvasRenderObject{
                .id = SceneObjectId(next_id++),
                .node_id = {},
                .element_id = wire_iid,
                .kind = CanvasRenderObjectKind::Wire,
                .bounds = Rect{min_x - BBOX_PADDING, min_y - BBOX_PADDING,
                               max_x - min_x + 2.0f * BBOX_PADDING,
                               max_y - min_y + 2.0f * BBOX_PADDING},
                .stroke_width = visual::Wire::WIRE_THICKNESS,
            });

            constexpr float HIT_TOLERANCE = 4.0f;
            for (size_t i = 0; i + 1 < polyline.size(); ++i) {
                const auto& p0 = polyline[i];
                const auto& p1 = polyline[i + 1];

                CanvasHitObject wh{};
                wh.id = SceneObjectId(next_id++);
                wh.element_id = wire_iid;
                wh.kind = CanvasHitObjectKind::WireSegment;
                wh.shape = HitShapeKind::Rectangle;
                wh.bounds = Rect{std::min(p0.x, p1.x) - HIT_TOLERANCE,
                                   std::min(p0.y, p1.y) - HIT_TOLERANCE,
                                   std::max(p0.x, p1.x) - std::min(p0.x, p1.x) + 2.0f * HIT_TOLERANCE,
                                   std::max(p0.y, p1.y) - std::min(p0.y, p1.y) + 2.0f * HIT_TOLERANCE};
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
        constexpr float RP_SIZE = 4.0f;
        const Rect rp_bounds = point_to_rect(rp_pos, RP_SIZE);

        // Find the owning Wire and compute routing-point index
        ui::InternedId wire_iid;
        size_t rp_idx = 0;
        if (auto* parent_widget = rp->parent()) {
            if (auto* wire = dynamic_cast<const visual::Wire*>(parent_widget)) {
                wire_iid = interner.intern(wire->id());
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
            .bounds = rp_bounds,
            .fill_color = 0xFF888888,
        });

        CanvasHitObject rph{};
        rph.id = SceneObjectId(next_id++);
        rph.node_id = rp_node_iid;
        rph.kind = CanvasHitObjectKind::RoutingPoint;
        rph.shape = HitShapeKind::Rectangle;
        rph.bounds = rp_bounds;
        rph.rp_wire_id = wire_iid;
        rph.rp_index = rp_idx;
        snapshot.hit_objects.push_back(std::move(rph));
    }
    // ---- Node-like widgets (NodeWidget, RefNodeWidget, BusNodeWidget, TextNodeWidget, GroupNodeWidget) ----
    else if (is_node_like(widget)) {
        ui::InternedId node_iid = node_widget_iid(widget, interner);
        const Rect node_bounds = widget_bounds(widget);

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
                    case SceneRenderObjectKind::ContentPaint:
                        kind = CanvasRenderObjectKind::ContentPaint;
                        break;
                }

                snapshot.render_objects.push_back(CanvasRenderObject{
                    .id = SceneObjectId(next_id++),
                    .node_id = node_iid,
                    .element_id = sem_obj.element_id,
                    .kind = kind,
                    .bounds = offset_rect(sem_obj.bounds, content_offset),
                    .text = sem_obj.text,
                    .fill_color = sem_obj.fill_color,
                    .stroke_color = sem_obj.stroke_color,
                    .stroke_width = sem_obj.stroke_width,
                    .inset = sem_obj.inset,
                    .text_size = sem_obj.text_size,
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
            constexpr float HANDLE_SIZE = 8.0f;
            const auto min_pt = widget.worldMin();
            const auto max_pt = widget.worldMax();
            const ui::Pt nwp = widget.worldPos();
            const ui::Pt nsz = widget.size();

            struct { Rect rect; ResizeCorner corner; } handles[] = {
                {Rect{min_pt.x, min_pt.y, HANDLE_SIZE, HANDLE_SIZE}, ResizeCorner::TopLeft},
                {Rect{max_pt.x - HANDLE_SIZE, min_pt.y, HANDLE_SIZE, HANDLE_SIZE}, ResizeCorner::TopRight},
                {Rect{min_pt.x, max_pt.y - HANDLE_SIZE, HANDLE_SIZE, HANDLE_SIZE}, ResizeCorner::BottomLeft},
                {Rect{max_pt.x - HANDLE_SIZE, max_pt.y - HANDLE_SIZE, HANDLE_SIZE, HANDLE_SIZE}, ResizeCorner::BottomRight},
            };
            for (const auto& h : handles) {
                CanvasHitObject rh{};
                rh.id = SceneObjectId(next_id++);
                rh.node_id = node_iid;
                rh.kind = CanvasHitObjectKind::ResizeHandle;
                rh.shape = HitShapeKind::Rectangle;
                rh.bounds = h.rect;
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

// ============================================================
// Geometry helpers for snapshot hit testing
// (distance / distance_to_segment reuse visual::hit_math)
// ============================================================

using visual::hit_math::distance;
using visual::hit_math::distance_to_segment;

bool point_in_rect(ui::Pt p, const Rect& r) {
    return p.x >= r.x && p.x <= r.x + r.w &&
           p.y >= r.y && p.y <= r.y + r.h;
}

bool point_in_circle(ui::Pt p, const Rect& bounds) {
    float cx = bounds.x + bounds.w * 0.5f;
    float cy = bounds.y + bounds.h * 0.5f;
    float radius = std::min(bounds.w, bounds.h) * 0.5f;
    float dx = p.x - cx;
    float dy = p.y - cy;
    return (dx * dx + dy * dy) <= (radius * radius);
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

visual::HitResult hit_test_canvas_scene(const CanvasSceneSnapshot& snapshot, ui::Pt world_pos,
                                        const ui::StringInterner& interner) {
    // We scan all hit objects and track best match per priority level.
    // Priority (descending): Port > RoutingPoint > ResizeHandle > NodeBody > WireSegment.

    const CanvasHitObject* best_port = nullptr;
    const CanvasHitObject* best_routing_point = nullptr;
    const CanvasHitObject* best_resize_handle = nullptr;
    const CanvasHitObject* best_node = nullptr;
    const CanvasHitObject* best_wire = nullptr;
    float best_wire_dist = 1e9f;

    constexpr float PORT_HIT_RADIUS = visual::PortConstants::HIT_RADIUS;
    constexpr float RP_HIT_RADIUS = 10.0f;
    constexpr float WIRE_TOLERANCE = 5.0f;

    for (const auto& obj : snapshot.hit_objects) {
        switch (obj.kind) {
            case CanvasHitObjectKind::Port: {
                float cx = obj.bounds.x + obj.bounds.w * 0.5f;
                float cy = obj.bounds.y + obj.bounds.h * 0.5f;
                ui::Pt center(cx, cy);
                if (distance(world_pos, center) <= PORT_HIT_RADIUS) {
                    best_port = &obj;
                }
                break;
            }
            case CanvasHitObjectKind::RoutingPoint: {
                float cx = obj.bounds.x + obj.bounds.w * 0.5f;
                float cy = obj.bounds.y + obj.bounds.h * 0.5f;
                ui::Pt center(cx, cy);
                if (distance(world_pos, center) <= RP_HIT_RADIUS) {
                    best_routing_point = &obj;
                }
                break;
            }
            case CanvasHitObjectKind::ResizeHandle: {
                if (point_in_rect(world_pos, obj.bounds)) {
                    best_resize_handle = &obj;
                }
                break;
            }
            case CanvasHitObjectKind::NodeBody: {
                if (obj.is_group) {
                    // GroupNodeWidget border-only hit semantics: only title bar and border margins.
                    float x0 = obj.bounds.x, y0 = obj.bounds.y;
                    float x1 = x0 + obj.bounds.w, y1 = y0 + obj.bounds.h;
                    if (world_pos.x < x0 || world_pos.x > x1 ||
                        world_pos.y < y0 || world_pos.y > y1)
                        break;

                    constexpr float title_h = editor_constants::GROUP_TITLE_PADDING * 2
                                            + editor_constants::Font::Medium;
                    constexpr float m = editor_constants::GROUP_BORDER_HIT_MARGIN;

                    bool on_title = (world_pos.y <= y0 + title_h);
                    bool on_border = (world_pos.x <= x0 + m || world_pos.x >= x1 - m ||
                                      world_pos.y >= y1 - m);
                    if (on_title || on_border) {
                        best_node = &obj;
                    }
                } else if (point_in_rect(world_pos, obj.bounds)) {
                    // If multiple nodes overlap, prefer the last one (higher render layer).
                    best_node = &obj;
                }
                break;
            }
            case CanvasHitObjectKind::WireSegment: {
                float d = distance_to_segment(world_pos, obj.segment_p0, obj.segment_p1);
                if (d < WIRE_TOLERANCE && d < best_wire_dist) {
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
            .node_id = interner.resolve(best_port->node_id),
            .port_name = interner.resolve(best_port->element_id),
            .side = best_port->port_side,
            .type = best_port->port_type,
            .center = ui::Pt(cx, cy),
        };
    }

    // --- Priority 2: Routing points ---
    if (best_routing_point) {
        float cx = best_routing_point->bounds.x + best_routing_point->bounds.w * 0.5f;
        float cy = best_routing_point->bounds.y + best_routing_point->bounds.h * 0.5f;
        return visual::HitRoutingPoint{
            .wire_id = interner.resolve(best_routing_point->rp_wire_id),
            .index = best_routing_point->rp_index,
            .world_pos = ui::Pt(cx, cy),
        };
    }

    // --- Priority 3: Resize handles ---
    if (best_resize_handle) {
        return visual::HitResizeHandle{
            .node_id = interner.resolve(best_resize_handle->node_id),
            .corner = best_resize_handle->corner,
            .world_pos = best_resize_handle->node_world_pos,
            .size = best_resize_handle->node_size,
        };
    }

    // --- Priority 4: Nodes ---
    if (best_node) {
        visual::HitNode hit{};
        hit.node_id = interner.resolve(best_node->node_id);
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
            .wire_id = interner.resolve(best_wire->element_id),
            .segment = best_wire->segment_index,
        };
    }

    return visual::HitEmpty{};
}

visual::HitResult hit_test_canvas_scene_ports(const CanvasSceneSnapshot& snapshot, ui::Pt world_pos,
                                              const ui::StringInterner& interner) {
    constexpr float PORT_HIT_RADIUS = visual::PortConstants::HIT_RADIUS;

    for (const auto& obj : snapshot.hit_objects) {
        if (obj.kind != CanvasHitObjectKind::Port) continue;

        float cx = obj.bounds.x + obj.bounds.w * 0.5f;
        float cy = obj.bounds.y + obj.bounds.h * 0.5f;
        ui::Pt center(cx, cy);
        if (distance(world_pos, center) <= PORT_HIT_RADIUS) {
            return visual::HitPort{
                .node_id = interner.resolve(obj.node_id),
                .port_name = interner.resolve(obj.element_id),
                .side = obj.port_side,
                .type = obj.port_type,
                .center = center,
            };
        }
    }

    return visual::HitEmpty{};
}

} // namespace editor::presentation

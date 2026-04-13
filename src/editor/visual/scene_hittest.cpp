#include "scene_hittest.h"
#include "scene.h"
#include "ui/core/grid.h"
#include "widget.h"
#include "wire/wire.h"
#include "wire/routing_point.h"
#include "port/visual_port.h"
#include "node/group_node_widget.h"
#include "node/visual_node.h"
#include "editor/layout_constants.h"
#include "editor/visual/presentation/semantic_scene_hittest.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace visual {

// ============================================================
// Pass helpers (file-local)
// ============================================================

/// Check resize handles on resizable widgets among candidates.
static std::optional<HitResult> hit_test_resize_handles(
        const std::vector<ui::Widget*>& candidates, Pt world_pos) {
    constexpr float R = editor_constants::RESIZE_HANDLE_HIT_RADIUS;
    for (ui::Widget* uw : candidates) {
        auto* w = static_cast<Widget*>(uw);
        if (!w->isResizable()) continue;

        Pt mn = w->worldMin();
        Pt mx = w->worldMax();

        struct { Pt center; ResizeCorner corner; } corners[] = {
            {{mn.x, mn.y}, ResizeCorner::TopLeft},
            {{mx.x, mn.y}, ResizeCorner::TopRight},
            {{mn.x, mx.y}, ResizeCorner::BottomLeft},
            {{mx.x, mx.y}, ResizeCorner::BottomRight},
        };

        for (const auto& c : corners) {
            if (hit_math::distance(world_pos, c.center) <= R) {
                return HitResizeHandle{w->id(), c.corner, w->worldPos(), w->size()};
            }
        }
    }
    return std::nullopt;
}

/// Find the best node widget (highest renderLayer) at world_pos among candidates.
static Widget* hit_test_node_body(
        const std::vector<ui::Widget*>& candidates, Pt world_pos) {
    Widget* best = nullptr;
    for (ui::Widget* uw : candidates) {
        auto* w = static_cast<Widget*>(uw);
        if (dynamic_cast<Wire*>(w)) continue;
        if (dynamic_cast<Port*>(w)) continue;
        if (dynamic_cast<RoutingPoint*>(w)) continue;

        if (auto* group = dynamic_cast<GroupNodeWidget*>(w)) {
            if (!group->containsBorder(world_pos)) continue;
        } else {
            if (!w->contains(world_pos)) continue;
        }

        if (!best || w->renderLayer() > best->renderLayer()) {
            best = w;
        }
    }
    return best;
}

// ============================================================
// Primary hit test
// ============================================================

HitResult hit_test(const Scene& scene, Pt world_pos) {
    // Query margin covers the largest hit radius
    float margin = std::max({hit_constants::PORT_RADIUS,
                             hit_constants::ROUTING_POINT_RADIUS,
                             hit_constants::WIRE_TOLERANCE});

    auto candidates = scene.grid().query(world_pos, margin);

    // --- Pass 1: Ports (highest priority) ---
    for (ui::Widget* uw : candidates) {
        auto* w = static_cast<Widget*>(uw);
        if (auto* port = dynamic_cast<Port*>(w)) {
            Pt center = port->worldPos() + Pt(PortConstants::RADIUS, PortConstants::RADIUS);
            if (hit_math::distance(world_pos, center) <= hit_constants::PORT_RADIUS) {
                return HitPort{
                    port->rootAncestorId(),
                    port->name(),
                    port->side(),
                    port->type(),
                    center,
                };
            }
        }
    }

    // --- Pass 2: Routing points ---
    for (ui::Widget* uw : candidates) {
        auto* w = static_cast<Widget*>(uw);
        if (auto* rp = dynamic_cast<RoutingPoint*>(w)) {
            if (hit_math::distance(world_pos, rp->worldPos()) <= hit_constants::ROUTING_POINT_RADIUS) {
                // Find the owning Wire and index
                std::string_view wire_id;
                size_t index = 0;
                if (auto* parent = rp->parent()) {
                    auto* wire = dynamic_cast<Wire*>(parent);
                    if (wire) {
                        wire_id = wire->id();
                        for (size_t i = 0; i < wire->children().size(); ++i) {
                            if (wire->children()[i].get() == rp) { index = i; break; }
                        }
                    }
                }
                return HitRoutingPoint{wire_id, index, rp->worldPos()};
            }
        }
    }

    // --- Pass 4: Resize handles on resizable widgets ---
    if (auto rh = hit_test_resize_handles(candidates, world_pos)) {
        return *rh;
    }

    // --- Pass 5: Nodes / generic clickable widgets (AABB) ---
    if (auto* node = hit_test_node_body(candidates, world_pos)) {
        HitNode hit{node->id(), node->worldPos(), node->size()};
        if (auto* visual_node = dynamic_cast<NodeWidget*>(node)) {
            hit.content_bounds = visual_node->contentBounds();
            hit.content_snapshot = visual_node->content_semantic_snapshot();
            hit.renders_content_from_semantic_snapshot = visual_node->renders_content_from_semantic_snapshot();

            if (hit.renders_content_from_semantic_snapshot && !hit.content_snapshot.hit_objects.empty()) {
                const Pt local(world_pos.x - hit.world_pos.x,
                               world_pos.y - hit.world_pos.y);
                auto semantic_hit = editor::presentation::hit_test_semantic_scene(hit.content_snapshot, local);
                if (auto* content_region = std::get_if<editor::presentation::SemanticHitContentRegion>(&semantic_hit)) {
                    if (content_region->object && !content_region->object->interactions.empty()) {
                        const auto& binding = content_region->object->interactions.front();
                        HitContentInteraction interaction;
                        interaction.kind = binding.kind;
                        interaction.primary_min = binding.min_value;
                        interaction.primary_max = binding.max_value;
                        interaction.steps = static_cast<int>(binding.step);
                        if (interaction.steps < 2) interaction.steps = 2;
                        hit.content_interaction = interaction;
                    }
                }
            }
        }
        return hit;
    }

    // --- Pass 6: Wire segments (lowest priority, fine-grained) ---
    for (ui::Widget* uw : candidates) {
        auto* w = static_cast<Widget*>(uw);
        if (auto* wire = dynamic_cast<Wire*>(w)) {
            const auto& pts = wire->polyline();
            if (pts.size() < 2) continue;

            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                if (hit_math::distance_to_segment(world_pos, pts[i], pts[i + 1])
                        < hit_constants::WIRE_TOLERANCE) {
                    return HitWire{wire->id(), i};
                }
            }
        }
    }

    return HitEmpty{};
}

// ============================================================
// Port-only hit test
// ============================================================

HitResult hit_test_ports(const Scene& scene, Pt world_pos) {
    auto candidates = scene.grid().query(world_pos, hit_constants::PORT_RADIUS);

    for (ui::Widget* uw : candidates) {
        auto* w = static_cast<Widget*>(uw);
        if (auto* port = dynamic_cast<Port*>(w)) {
            Pt center = port->worldPos() + Pt(PortConstants::RADIUS, PortConstants::RADIUS);
            if (hit_math::distance(world_pos, center) <= hit_constants::PORT_RADIUS) {
                return HitPort{
                    port->rootAncestorId(),
                    port->name(),
                    port->side(),
                    port->type(),
                    center,
                };
            }
        }
    }

    return HitEmpty{};
}

} // namespace visual

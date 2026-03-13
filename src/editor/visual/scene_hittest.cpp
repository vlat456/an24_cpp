#include "scene_hittest.h"
#include "scene.h"
#include "grid.h"
#include "widget.h"
#include "wire/wire.h"
#include "wire/wire_end.h"
#include "wire/routing_point.h"
#include "port/visual_port.h"
#include "node/group_node_widget.h"
#include "editor/layout_constants.h"
#include <algorithm>
#include <cmath>

namespace visual {

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
            Pt center = port->worldPos() + Pt(Port::RADIUS, Port::RADIUS);
            if (hit_math::distance(world_pos, center) <= hit_constants::PORT_RADIUS) {
                return HitPort{port};
            }
        }
    }

    // --- Pass 2: Routing points ---
    for (ui::Widget* uw : candidates) {
        auto* w = static_cast<Widget*>(uw);
        if (auto* rp = dynamic_cast<RoutingPoint*>(w)) {
            if (hit_math::distance(world_pos, rp->worldPos()) <= hit_constants::ROUTING_POINT_RADIUS) {
                // Find the owning Wire and index
                Wire* wire = nullptr;
                size_t index = 0;
                if (auto* parent = rp->parent()) {
                    wire = dynamic_cast<Wire*>(parent);
                    if (wire) {
                        for (size_t i = 0; i < wire->children().size(); ++i) {
                            if (wire->children()[i].get() == rp) { index = i; break; }
                        }
                    }
                }
                return HitRoutingPoint{rp, wire, index};
            }
        }
    }

    // --- Pass 3: Resize handles on resizable widgets ---
    // Must run before the node-body pass because GroupNodeWidget's
    // containsBorder() rejects interior clicks, which can filter out
    // clicks near corners that fall within the resize-handle radius
    // but outside the narrow border margin.
    {
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
                    return HitResizeHandle{w, c.corner};
                }
            }
        }
    }

    // --- Pass 4: Nodes / generic clickable widgets (AABB) ---
    // Skip Wires, Ports, and RoutingPoints — they have their own passes.
    // Among multiple hits, prefer the widget with the highest renderLayer
    // (frontmost in draw order).
    Widget* best = nullptr;
    for (ui::Widget* uw : candidates) {
        auto* w = static_cast<Widget*>(uw);
        if (dynamic_cast<Wire*>(w)) continue;
        if (dynamic_cast<Port*>(w)) continue;
        if (dynamic_cast<RoutingPoint*>(w)) continue;

        // GroupNodeWidget uses border-only hit testing
        if (auto* group = dynamic_cast<GroupNodeWidget*>(w)) {
            if (!group->containsBorder(world_pos)) continue;
        } else {
            if (!w->contains(world_pos)) continue;
        }

        // Prefer higher layer (visually on top)
        if (!best || w->renderLayer() > best->renderLayer()) {
            best = w;
        }
    }
    if (best) {
        return HitNode{best};
    }

    // --- Pass 5: Wire segments (lowest priority, fine-grained) ---
    for (ui::Widget* uw : candidates) {
        auto* w = static_cast<Widget*>(uw);
        if (auto* wire = dynamic_cast<Wire*>(w)) {
            const auto& pts = wire->polyline();
            if (pts.size() < 2) continue;

            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                if (hit_math::distance_to_segment(world_pos, pts[i], pts[i + 1])
                        < hit_constants::WIRE_TOLERANCE) {
                    return HitWire{wire, i};
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
            Pt center = port->worldPos() + Pt(Port::RADIUS, Port::RADIUS);
            if (hit_math::distance(world_pos, center) <= hit_constants::PORT_RADIUS) {
                return HitPort{port};
            }
        }
    }

    return HitEmpty{};
}

} // namespace visual

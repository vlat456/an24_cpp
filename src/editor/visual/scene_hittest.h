#pragma once

/// Scene-graph hit testing (Phase 7).
///
/// Queries the Grid for nearby clickable widgets, then applies fine-grained
/// geometric checks (distance to port center, wire polyline segments, etc.)
/// with priority ordering:
///     Port > RoutingPoint > ResizeHandle > Node > Wire segment > Empty.
///
/// Returns a std::variant<> so the caller can pattern-match on the result.

#include "ui/math/pt.h"
#include "editor/input/input_types.h"
#include "visual/port/visual_port.h"
#include <variant>
#include <cstddef>
#include <cmath>

namespace visual {

namespace hit_constants {
    constexpr float PORT_RADIUS = PortConstants::HIT_RADIUS;
    constexpr float ROUTING_POINT_RADIUS = 10.0f;
    constexpr float WIRE_TOLERANCE = 5.0f;
}

using ui::Pt;

class Scene;
class Widget;
class Wire;
class RoutingPoint;
class Port;
class NodeWidget;

struct HitEmpty {};
struct HitNode {
    std::string_view node_id;
    Pt world_pos{};
    Pt size{};
};
struct HitPort {
    std::string_view node_id;
    std::string_view port_name;
    bp2::PortSide side = bp2::PortSide::Input;
    PortType type = PortType::Any;
    Pt center{};
};
struct HitWire {
    std::string_view wire_id;
    size_t segment = 0;
};
struct HitRoutingPoint {
    std::string_view wire_id;
    size_t index = 0;
    Pt world_pos{};
};
struct HitResizeHandle {
    std::string_view node_id;
    ResizeCorner corner = ResizeCorner::BottomRight;
    Pt world_pos{};
    Pt size{};
};

using HitResult = std::variant<HitEmpty, HitNode, HitPort, HitWire, HitRoutingPoint, HitResizeHandle>;

// ============================================================
// Hit testing functions
// ============================================================

/// Primary hit test: returns the highest-priority widget under world_pos.
HitResult hit_test(const Scene& scene, Pt world_pos);

/// Port-only hit test: only checks ports, ignoring all other types.
HitResult hit_test_ports(const Scene& scene, Pt world_pos);

// ============================================================
// Geometry utilities (self-contained, no old-system dependencies)
// ============================================================

namespace hit_math {

inline float distance(Pt a, Pt b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

inline float distance_to_segment(Pt p, Pt a, Pt b) {
    float ab_x = b.x - a.x;
    float ab_y = b.y - a.y;
    float len_sq = ab_x * ab_x + ab_y * ab_y;
    if (len_sq < 1e-6f) return distance(p, a);

    float t = ((p.x - a.x) * ab_x + (p.y - a.y) * ab_y) / len_sq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    Pt closest(a.x + t * ab_x, a.y + t * ab_y);
    return distance(p, closest);
}

} // namespace hit_math

} // namespace visual

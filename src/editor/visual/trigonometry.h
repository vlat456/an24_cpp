#pragma once

#include "data/pt.h"
#include "data/node.h"
#include "data/wire.h"
#include "data/blueprint.h"
#include "visual/node/visual_node_cache.h"
#include <cmath>
#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace editor_math {

inline Pt get_port_position(const Node& node, const char* port_name,
                            const std::vector<Wire>& wires,
                            const char* wire_id,
                            VisualNodeCache& cache) {
    VisualNode* visual = cache.getOrCreate(node, wires);
    const VisualPort* port = visual->resolveWirePort(port_name, wire_id);
    if (port) return port->worldPosition();
    // Fallback: center of node
    return Pt(visual->getPosition().x + visual->getSize().x / 2,
              visual->getPosition().y + visual->getSize().y / 2);
}

/// Resolve both endpoints of a wire to world positions.
/// Returns nullopt if either endpoint node is missing or not in the given group.
inline std::optional<std::pair<Pt, Pt>> resolve_wire_endpoints(
        const Wire& wire, const Blueprint& bp,
        const std::string& group_id, VisualNodeCache& cache) {
    const Node* sn = bp.find_node(wire.start.node_id.c_str());
    const Node* en = bp.find_node(wire.end.node_id.c_str());
    if (!sn || !en) return std::nullopt;
    if (sn->group_id != group_id || en->group_id != group_id) return std::nullopt;
    Pt start_pos = get_port_position(*sn, wire.start.port_name.c_str(),
                                     bp.wires, wire.id.c_str(), cache);
    Pt end_pos   = get_port_position(*en, wire.end.port_name.c_str(),
                                     bp.wires, wire.id.c_str(), cache);
    return std::pair{start_pos, end_pos};
}

// Расстояние между точками
inline float distance(Pt a, Pt b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Расстояние от точки до отрезка
inline float distance_to_segment(Pt p, Pt a, Pt b) {
    float ab_x = b.x - a.x;
    float ab_y = b.y - a.y;
    float ab_len_sq = ab_x * ab_x + ab_y * ab_y;

    if (ab_len_sq < 1e-6f) {
        return distance(p, a);
    }

    float t = ((p.x - a.x) * ab_x + (p.y - a.y) * ab_y) / ab_len_sq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    Pt closest(a.x + t * ab_x, a.y + t * ab_y);
    return distance(p, closest);
}

/// Привязка точки к сетке (snap to grid)
inline Pt snap_to_grid(Pt pos, float grid_step) {
    return Pt(
        std::round(pos.x / grid_step) * grid_step,
        std::round(pos.y / grid_step) * grid_step
    );
}

} // namespace editor_math

#pragma once

#include "data/pt.h"
#include <vector>

/// Direction of wire segment
enum class WireDir {
    Horizontal,
    Vertical
};

/// A crossing point on a wire segment
struct WireCrossing {
    Pt pos;
    WireDir dir;  // direction of THIS wire at crossing
};

/// Check if two line segments cross (proper intersection, not just touching)
/// Returns the intersection point if they cross, along with directions of both segments
inline std::optional<std::tuple<Pt, WireDir, WireDir>> segment_crosses_with_dir(
    Pt a0, Pt a1, Pt b0, Pt b1) {

    float d1x = a1.x - a0.x;
    float d1y = a1.y - a0.y;
    float d2x = b1.x - b0.x;
    float d2y = b1.y - b0.y;

    // Cross product of direction vectors
    float cross = d1x * d2y - d1y * d2x;
    if (std::abs(cross) < 1e-6f) {
        return std::nullopt; // Parallel or collinear
    }

    float dx = b0.x - a0.x;
    float dy = b0.y - a0.y;
    float t = (dx * d2y - dy * d2x) / cross;
    float u = (dx * d1y - dy * d1x) / cross;

    // Strict interior: exclude endpoints to avoid false crossings at corners
    float eps = 0.01f;
    if (t > eps && t < 1.0f - eps && u > eps && u < 1.0f - eps) {
        Pt intersection(a0.x + t * d1x, a0.y + t * d1y);

        // Determine directions (horizontal if mostly X movement, vertical if mostly Y)
        WireDir dir1 = (std::abs(d1x) >= std::abs(d1y)) ? WireDir::Horizontal : WireDir::Vertical;
        WireDir dir2 = (std::abs(d2x) >= std::abs(d2y)) ? WireDir::Horizontal : WireDir::Vertical;

        return std::make_tuple(intersection, dir1, dir2);
    }
    return std::nullopt;
}

/// Check if two line segments cross (proper intersection, not just touching)
/// Returns the intersection point if they cross
inline std::optional<Pt> segment_crosses(Pt a0, Pt a1, Pt b0, Pt b1) {
    auto result = segment_crosses_with_dir(a0, a1, b0, b1);
    if (result) {
        return std::get<0>(*result);
    }
    return std::nullopt;
}

/// Find all crossing points on wire `wire_idx` caused by other wires
/// Only wires with index > `wire_idx` will draw arcs (to avoid double-drawing)
inline std::vector<WireCrossing> find_wire_crossings(
    size_t wire_idx,
    const std::vector<std::vector<Pt>>& polylines) {

    std::vector<WireCrossing> crossings;

    if (wire_idx >= polylines.size()) return crossings;
    const auto& my_poly = polylines[wire_idx];
    if (my_poly.size() < 2) return crossings;

    for (size_t other_idx = 0; other_idx < polylines.size(); other_idx++) {
        if (other_idx == wire_idx) continue;
        // Higher-index wire draws the arc
        if (wire_idx < other_idx) continue;

        const auto& other_poly = polylines[other_idx];
        if (other_poly.size() < 2) continue;

        // Check all segment pairs
        for (size_t i = 0; i + 1 < my_poly.size(); i++) {
            for (size_t j = 0; j + 1 < other_poly.size(); j++) {
                auto result = segment_crosses_with_dir(my_poly[i], my_poly[i+1],
                                                      other_poly[j], other_poly[j+1]);
                if (result) {
                    auto [pt, my_dir, other_dir] = *result;
                    // Store the direction of THIS wire (wire_idx)
                    crossings.push_back({pt, my_dir});
                }
            }
        }
    }

    return crossings;
}

/// Build world-space polyline for a wire (start port -> RPs -> end port)
inline std::vector<Pt> wire_world_polyline(
    const struct Wire& wire,
    const std::vector<struct Node>& nodes,
    const std::vector<Pt>& default_port_pos) {

    std::vector<Pt> pts;

    // Find start position - use default or look up
    // For now, use default positions if available
    if (wire.routing_points.size() >= 2) {
        // This is a placeholder - actual implementation needs port lookup
        pts = wire.routing_points;
    }

    return pts;
}

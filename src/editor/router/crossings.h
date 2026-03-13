#pragma once

#include "ui/math/pt.h"
#include "ui/core/small_vector.h"
#include <vector>
#include <optional>
#include <cmath>

/// Direction of the wire segment at a crossing point
enum class SegDir { Horiz, Vert, Unknown };

/// A crossing point on a wire segment
struct WireCrossing {
    ui::Pt pos;
    SegDir my_seg_dir;  ///< Direction of the wire segment that owns this crossing
    bool draw_arc;      ///< true = draw arc (jumper), false = gap only (crossed under)
};

/// Determine segment direction
inline SegDir segment_direction(ui::Pt a, ui::Pt b) {
    float dx = std::abs(b.x - a.x);
    float dy = std::abs(b.y - a.y);
    if (dx > dy) return SegDir::Horiz;
    if (dy > dx) return SegDir::Vert;
    return SegDir::Unknown;
}

/// Check if two line segments cross (proper intersection, not just touching)
/// Returns the intersection point if they cross
inline std::optional<ui::Pt> segment_crosses(ui::Pt a0, ui::Pt a1, ui::Pt b0, ui::Pt b1) {
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
        return ui::Pt(a0.x + t * d1x, a0.y + t * d1y);
    }
    return std::nullopt;
}

/// Find crossings between two polylines.
/// `my_draws_arc` controls whether crossings on `my_poly` are arcs or gaps.
inline void find_pairwise_crossings(
    const std::vector<ui::Pt>& my_poly,
    const std::vector<ui::Pt>& other_poly,
    bool my_draws_arc,
    std::vector<WireCrossing>& out) {

    if (my_poly.size() < 2 || other_poly.size() < 2) return;

    for (size_t i = 0; i + 1 < my_poly.size(); i++) {
        for (size_t j = 0; j + 1 < other_poly.size(); j++) {
            auto pt = segment_crosses(my_poly[i], my_poly[i + 1],
                                      other_poly[j], other_poly[j + 1]);
            if (pt) {
                SegDir my_dir = segment_direction(my_poly[i], my_poly[i + 1]);
                out.push_back({*pt, my_dir, my_draws_arc});
            }
        }
    }
}

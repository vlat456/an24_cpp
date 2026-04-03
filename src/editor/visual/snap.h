#pragma once

#include "ui/math/pt.h"
#include "data/port.h"
#include "editor/layout_constants.h"
#include <cmath>

namespace editor_math {

using ui::Pt;

/// Given two center positions, determine which edge of `from_center` faces
/// `to_center` (the dominant axis wins; ties go to horizontal).
inline PortLayoutSide side_from_relative_position(Pt from_center, Pt to_center) {
    const float dx = to_center.x - from_center.x;
    const float dy = to_center.y - from_center.y;
    if (std::abs(dx) >= std::abs(dy)) {
        return (dx >= 0.0f) ? PortLayoutSide::Right : PortLayoutSide::Left;
    }
    return (dy >= 0.0f) ? PortLayoutSide::Bottom : PortLayoutSide::Top;
}

/// Snap a position to the user-facing grid (round to nearest).
inline Pt snap_to_grid(Pt pos, float grid_step) {
    if (grid_step < 1e-6f) return pos;  // guard against zero/negative
    return Pt(
        std::round(pos.x / grid_step) * grid_step,
        std::round(pos.y / grid_step) * grid_step
    );
}

/// Snap a position to half-grid steps (between the user-facing grid lines).
inline Pt snap_to_half_grid(Pt pos, float grid_step) {
    if (grid_step < 1e-6f) return pos;
    float half = grid_step * 0.5f;
    return Pt(
        std::round(pos.x / half) * half,
        std::round(pos.y / half) * half
    );
}

/// Snap a position to the internal port-layout grid (round to nearest).
inline Pt snap_to_layout_grid(Pt pos) {
    constexpr float g = editor_constants::PORT_LAYOUT_GRID;
    return Pt(
        std::round(pos.x / g) * g,
        std::round(pos.y / g) * g
    );
}

/// Snap a size to the internal port-layout grid (round up to nearest).
inline Pt snap_size_to_layout_grid(Pt size) {
    constexpr float g = editor_constants::PORT_LAYOUT_GRID;
    return Pt(
        std::ceil(size.x / g) * g,
        std::ceil(size.y / g) * g
    );
}

/// Snap a scalar size to the internal port-layout grid (round up to nearest).
inline float snap_size_to_layout_grid(float v) {
    constexpr float g = editor_constants::PORT_LAYOUT_GRID;
    return std::ceil(v / g) * g;
}

} // namespace editor_math

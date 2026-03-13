#pragma once

#include "ui/math/pt.h"
#include "editor/layout_constants.h"
#include <cmath>

namespace editor_math {

using ui::Pt;

/// Snap a position to the user-facing grid (round to nearest).
inline Pt snap_to_grid(Pt pos, float grid_step) {
    return Pt(
        std::round(pos.x / grid_step) * grid_step,
        std::round(pos.y / grid_step) * grid_step
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

} // namespace editor_math

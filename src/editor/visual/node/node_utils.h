#pragma once

#include "editor/data/pt.h"
#include "editor/layout_constants.h"
#include <cmath>

namespace an24 {
namespace node_utils {

inline Pt snap_to_grid(Pt pos) {
    return Pt(
        std::round(pos.x / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID,
        std::round(pos.y / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID
    );
}

inline Pt snap_size_to_grid(Pt size) {
    return Pt(
        std::ceil(size.x / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID,
        std::ceil(size.y / editor_constants::PORT_LAYOUT_GRID) * editor_constants::PORT_LAYOUT_GRID
    );
}

} // namespace node_utils
} // namespace an24

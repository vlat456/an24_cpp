#pragma once
#include "ui/math/pt.h"
#include "blueprint_v2/blueprint/node_port.h"
#include <cmath>

namespace visual {

/// Arrow geometry for a port direction indicator.
struct PortArrowGeometry {
    Pt tip;
    Pt back1;
    Pt back2;
};

/// Compute arrowhead geometry for a port's direction arrow.
/// @param layout_side  Which edge the port sits on
/// @param direction    Input or Output (InOut draws no arrow)
/// @param center       Screen-space center of the port circle
/// @param radius       Port circle radius in screen pixels
/// @param arrow_offset Distance from center to arrow tip (screen pixels)
/// @param arrow_size   Arrowhead arm length (screen pixels)
inline PortArrowGeometry compute_port_arrow(
    bp2::PortLayoutSide layout_side,
    bp2::Direction direction,
    Pt center,
    float radius,
    float arrow_offset,
    float arrow_size)
{
    const bool is_output = (direction == bp2::Direction::Output);
    const bool horizontal = (layout_side == bp2::PortLayoutSide::Left ||
                             layout_side == bp2::PortLayoutSide::Right);

    PortArrowGeometry g{};
    if (horizontal) {
        const float interior_sign = (layout_side == bp2::PortLayoutSide::Left) ? 1.0f : -1.0f;
        const float arrow_x = center.x + interior_sign * arrow_offset;
        const float tip_sign = is_output ? -interior_sign : interior_sign;
        g.tip   = Pt(arrow_x, center.y);
        g.back1 = Pt(arrow_x - tip_sign * arrow_size, center.y - arrow_size);
        g.back2 = Pt(arrow_x - tip_sign * arrow_size, center.y + arrow_size);
    } else {
        const float interior_sign = (layout_side == bp2::PortLayoutSide::Top) ? 1.0f : -1.0f;
        const float arrow_y = center.y + interior_sign * arrow_offset;
        const float tip_sign = is_output ? -interior_sign : interior_sign;
        g.tip   = Pt(center.x, arrow_y);
        g.back1 = Pt(center.x - arrow_size, arrow_y - tip_sign * arrow_size);
        g.back2 = Pt(center.x + arrow_size, arrow_y - tip_sign * arrow_size);
    }
    return g;
}

/// Map layout side to arrow offset constant from PortConstants.
inline float arrow_offset_for_side(bp2::PortLayoutSide side) {
    switch (side) {
        case bp2::PortLayoutSide::Left:   return 2.5f;
        case bp2::PortLayoutSide::Right:  return 2.0f;
        case bp2::PortLayoutSide::Top:    return 2.5f;
        case bp2::PortLayoutSide::Bottom: return 1.5f;
    }
    return 0.0f;
}

} // namespace visual

#include "visual_port.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
#include "visual/render_context.h"

namespace visual {

Port::Port(std::string_view name, bp2::PortSide side, PortType type, bp2::PortLayoutSide layout_side)
    : name_(name), side_(side), type_(type), layout_side_(layout_side)
{
    setSize(Pt(PortConstants::RADIUS * 2, PortConstants::RADIUS * 2));
}

uint32_t Port::color() const {
    return render_theme::get_port_color(type_);
}

Pt Port::preferredSize(IDrawList*) const {
    return Pt(PortConstants::RADIUS * 2, PortConstants::RADIUS * 2);
}

void Port::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = ctx.world_to_screen(worldPos());
    float r = PortConstants::RADIUS * ctx.zoom;

    Pt center(pos.x + r, pos.y + r);
    dl->add_circle_filled(center, r, color(), 8);

    if (side_ == bp2::PortSide::InOut) return;

    // Arrow offset multiplier (relative to scaled radius)
    float arrow_offset_mult = 0;
    switch (layout_side_) {
        case bp2::PortLayoutSide::Left:   arrow_offset_mult = PortConstants::LEFT_ARROW_OFFSET; break;
        case bp2::PortLayoutSide::Right:  arrow_offset_mult = PortConstants::RIGHT_ARROW_OFFSET; break;
        case bp2::PortLayoutSide::Top:    arrow_offset_mult = PortConstants::TOP_ARROW_OFFSET; break;
        case bp2::PortLayoutSide::Bottom: arrow_offset_mult = PortConstants::BOTTOM_ARROW_OFFSET; break;
    }
    
    float arrow_offset = r * arrow_offset_mult;
    float arrow_size = PortConstants::ARROW_SIZE * ctx.zoom;
    float thickness = PortConstants::ARROW_THICKNESS * ctx.zoom;
    
    // Determine arrow direction:
    //   Output = data flows AWAY from node (tip points outward)
    //   Input  = data flows TOWARD node (tip points inward)
    //
    // The arrow sits on the interior side of the port (between circle and label).
    // "outward" means away from node center:
    //   Left  → outward = -x,  inward = +x
    //   Right → outward = +x,  inward = -x
    //   Top   → outward = -y,  inward = +y
    //   Bottom→ outward = +y,  inward = -y
    //
    // For Output: tip goes outward direction
    // For Input:  tip goes inward direction
    //
    // The arrow chevron is: two lines from tip to back1/back2.
    // If tip_dir > 0, backs are at tip - arrow_size (so tip is further in + direction).
    // If tip_dir < 0, backs are at tip + arrow_size (so tip is further in - direction).
    
    Pt tip, back1, back2;
    bool is_output = (side_ == bp2::PortSide::Output);
    
    bool horizontal = (layout_side_ == bp2::PortLayoutSide::Left || layout_side_ == bp2::PortLayoutSide::Right);
    
    if (horizontal) {
        // Arrow is along x-axis, positioned on interior side
        // Interior direction: Left→+x, Right→-x
        bool interior_is_positive = (layout_side_ == bp2::PortLayoutSide::Left);
        float interior_sign = interior_is_positive ? 1.0f : -1.0f;
        
        // Arrow base position: offset from center toward interior
        float arrow_x = center.x + interior_sign * arrow_offset;
        
        // Tip direction: output→outward (opposite of interior), input→inward (same as interior)
        float tip_sign = is_output ? -interior_sign : interior_sign;
        
        tip = Pt(arrow_x, center.y);
        back1 = Pt(arrow_x - tip_sign * arrow_size, center.y - arrow_size);
        back2 = Pt(arrow_x - tip_sign * arrow_size, center.y + arrow_size);
    } else {
        // Arrow is along y-axis, positioned on interior side
        // Interior direction: Top→+y, Bottom→-y
        bool interior_is_positive = (layout_side_ == bp2::PortLayoutSide::Top);
        float interior_sign = interior_is_positive ? 1.0f : -1.0f;
        
        // Arrow base position: offset from center toward interior
        float arrow_y = center.y + interior_sign * arrow_offset;
        
        // Tip direction: output→outward (opposite of interior), input→inward (same as interior)
        float tip_sign = is_output ? -interior_sign : interior_sign;
        
        tip = Pt(center.x, arrow_y);
        back1 = Pt(center.x - arrow_size, arrow_y - tip_sign * arrow_size);
        back2 = Pt(center.x + arrow_size, arrow_y - tip_sign * arrow_size);
    }
    
    dl->add_line(tip, back1, color(), thickness);
    dl->add_line(tip, back2, color(), thickness);
}

} // namespace visual

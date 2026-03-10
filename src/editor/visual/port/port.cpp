#include "visual/port/port.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include <cmath>

// ============================================================================
// VisualPort
// ============================================================================

VisualPort::VisualPort(const std::string& name,
                       PortSide side,
                       an24::PortType type,
                       const std::string& target_port)
    : name_(name)
    , target_port_(target_port)
    , label_(name)
    , side_(side)
    , type_(type)
    , world_pos_()
{
    width_ = RADIUS * 2;
    height_ = RADIUS * 2;
}

uint32_t VisualPort::color() const {
    return render_theme::get_port_color(type_);
}

Bounds VisualPort::calcBounds() const {
    return { world_pos_.x - RADIUS, world_pos_.y - RADIUS,
             RADIUS * 2, RADIUS * 2 };
}

// --- Compatibility ---

bool VisualPort::areTypesCompatible(an24::PortType a, an24::PortType b) {
    if (a == an24::PortType::Any || b == an24::PortType::Any)
        return true;
    return a == b;
}

bool VisualPort::areSidesCompatible(PortSide a, PortSide b) {
    // InOut connects to anything
    if (a == PortSide::InOut || b == PortSide::InOut)
        return true;
    // Input <-> Output only
    return a != b;
}

bool VisualPort::isCompatibleWith(const VisualPort& other) const {
    return areTypesCompatible(type_, other.type_) &&
           areSidesCompatible(side_, other.side_);
}

// --- Rendering ---

void VisualPort::render(IDrawList* dl, Pt origin, float zoom) const {
    float r = RADIUS * zoom;

    // Circle center: origin is the screen-space center of this port
    dl->add_circle_filled(origin, r, color(), 8);

    // Optional label
    if (!label_.empty() && label_side_ != LabelSide::None) {
        float gap = LABEL_GAP * zoom;
        float font = LABEL_FONT_SIZE * zoom;

        if (label_side_ == LabelSide::Right) {
            Pt label_pos(origin.x + r + gap, origin.y - font / 2);
            dl->add_text(label_pos, label_.c_str(), render_theme::COLOR_TEXT, font);
        } else { // Left
            Pt text_size = dl->calc_text_size(label_.c_str(), font);
            Pt label_pos(origin.x - r - gap - text_size.x, origin.y - font / 2);
            dl->add_text(label_pos, label_.c_str(), render_theme::COLOR_TEXT, font);
        }
    }
}

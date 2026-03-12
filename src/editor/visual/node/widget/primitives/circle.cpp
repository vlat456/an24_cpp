#include "visual/node/widget/primitives/circle.h"
#include "visual/renderer/draw_list.h"

Circle::Circle(float radius, uint32_t color)
    : radius_(radius), color_(color) {}

Pt Circle::getPreferredSize(IDrawList*) const {
    return Pt(radius_ * 2, radius_ * 2);
}

void Circle::render(IDrawList* dl, Pt origin, float zoom) const {
    if (!dl) return;
    float r = radius_ * zoom;
    Pt center(origin.x + width_ * zoom / 2, origin.y + height_ * zoom / 2);
    dl->add_circle_filled(center, r, color_);
}

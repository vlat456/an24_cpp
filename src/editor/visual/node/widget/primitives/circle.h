#pragma once

#include "visual/node/widget/widget_base.h"
#include <cstdint>

class Circle : public Widget {
public:
    Circle(float radius, uint32_t color);

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

private:
    float radius_;
    uint32_t color_;
};

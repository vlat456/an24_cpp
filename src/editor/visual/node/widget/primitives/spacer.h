#pragma once

#include "visual/node/widget/widget_base.h"

class Spacer : public Widget {
public:
    Spacer();

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;
};

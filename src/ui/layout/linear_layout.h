#pragma once

#include "ui/layout/linear_layout_algo.h"

namespace ui {

template <Axis axis>
class LinearLayout : public Widget {
public:
    Pt preferredSize(IDrawList* dl) const override {
        return detail::linearPreferredSize<axis>(this->children_, dl);
    }

    void layout(float available_width, float available_height) override {
        this->setSize(Pt(available_width, available_height));
        detail::linearLayout<axis>(this->children_, available_width, available_height);
    }

    void render(IDrawList* dl) const override {
        for (const auto& c : this->children()) {
            c->render(dl);
        }
    }
};

using Row = LinearLayout<Axis::Horizontal>;
using Column = LinearLayout<Axis::Vertical>;

} // namespace ui

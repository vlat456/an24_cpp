#pragma once

#include "../widget_base.h"
#include "../../edges.h"
#include <memory>

class Container : public Widget {
public:
    Container(std::unique_ptr<Widget> child, Edges margins);

    Pt getPreferredSize(IDrawList* dl) const override;
    void layout(float available_width, float available_height) override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    Widget* child() const { return child_.get(); }

private:
    std::unique_ptr<Widget> child_;
    Edges margins_;
};

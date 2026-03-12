#pragma once

#include "visual/node/widget/widget_base.h"
#include <vector>
#include <memory>

class Row : public Widget {
public:
    Widget* addChild(std::unique_ptr<Widget> child);

    Pt getPreferredSize(IDrawList* dl) const override;
    void layout(float available_width, float available_height) override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    size_t childCount() const { return children_.size(); }
    Widget* child(size_t i) const;

private:
    std::vector<std::unique_ptr<Widget>> children_;
};

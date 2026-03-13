#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "visual/node/edges.h"

namespace visual {

class Container : public Widget {
public:
    Container(Edges margins = {}) : margins_(margins) {}

    Pt preferredSize(IDrawList* dl) const override {
        if (children().empty()) return Pt(0, 0);
        Pt child_ps = children()[0]->preferredSize(dl);
        return Pt(child_ps.x + margins_.left + margins_.right,
                  child_ps.y + margins_.top + margins_.bottom);
    }

    void layout(float available_width, float available_height) override {
        setSize(Pt(available_width, available_height));
        if (!children().empty()) {
            float w = available_width - margins_.left - margins_.right;
            float h = available_height - margins_.top - margins_.bottom;
            auto& child_ptr = const_cast<std::vector<std::unique_ptr<Widget>>&>(children())[0];
            child_ptr->setLocalPos(Pt(margins_.left, margins_.top));
            child_ptr->layout(w, h);
        }
    }

    void render(IDrawList* dl, const RenderContext& ctx) const override {
        for (const auto& c : children()) {
            c->render(dl, ctx);
        }
    }

private:
    Edges margins_;
};

} // namespace visual

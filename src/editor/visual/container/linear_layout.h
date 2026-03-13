#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include <algorithm>

namespace visual {

enum class Axis { Horizontal, Vertical };

template <Axis axis>
class LinearLayout : public Widget {
public:
    Pt preferredSize(IDrawList* dl) const override {
        float sum = 0;
        float cross_max = 0;
        for (const auto& c : children()) {
            Pt ps = c->preferredSize(dl);
            sum += main(ps);
            cross_max = std::max(cross_max, cross(ps));
        }
        return make_pt(sum, cross_max);
    }

    void layout(float available_width, float available_height) override {
        setSize(Pt(available_width, available_height));

        float available_main = main_dim(available_width, available_height);
        float available_cross = cross_dim(available_width, available_height);

        float fixed_total = 0;
        int flex_count = 0;
        for (const auto& c : children()) {
            if (c->isFlexible()) {
                flex_count++;
            } else {
                fixed_total += main(c->preferredSize(nullptr));
            }
        }

        float remaining = std::max(0.0f, available_main - fixed_total);
        float flex_size = flex_count > 0 ? remaining / flex_count : 0;

        float pos = 0;
        for (auto& c : children()) {
            float child_main = c->isFlexible() ? flex_size : main(c->preferredSize(nullptr));
            if constexpr (axis == Axis::Horizontal) {
                c->setLocalPos(Pt(pos, 0));
                c->layout(child_main, available_cross);
            } else {
                c->setLocalPos(Pt(0, pos));
                c->layout(available_cross, child_main);
            }
            pos += child_main;
        }
    }

    void render(IDrawList* dl, const RenderContext& ctx) const override {
        for (const auto& c : children()) {
            static_cast<const Widget*>(c.get())->render(dl, ctx);
        }
    }

private:
    static float main(Pt p) {
        if constexpr (axis == Axis::Horizontal) return p.x;
        else return p.y;
    }
    static float cross(Pt p) {
        if constexpr (axis == Axis::Horizontal) return p.y;
        else return p.x;
    }
    static Pt make_pt(float main_val, float cross_val) {
        if constexpr (axis == Axis::Horizontal) return Pt(main_val, cross_val);
        else return Pt(cross_val, main_val);
    }
    static float main_dim(float w, float h) {
        if constexpr (axis == Axis::Horizontal) return w;
        else return h;
    }
    static float cross_dim(float w, float h) {
        if constexpr (axis == Axis::Horizontal) return h;
        else return w;
    }
};

using Row = LinearLayout<Axis::Horizontal>;
using Column = LinearLayout<Axis::Vertical>;

} // namespace visual

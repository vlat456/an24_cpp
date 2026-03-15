#pragma once

#include "ui/core/widget.h"
#include <algorithm>

namespace ui {

enum class Axis { Horizontal, Vertical };

namespace detail {

template <Axis axis>
struct AxisHelper {
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

/// Compute preferred size for a linear layout along the given axis.
/// Children are summed along main axis, max'd along cross axis.
template <Axis axis>
Pt linearPreferredSize(const std::vector<std::unique_ptr<Widget>>& children,
                       IDrawList* dl) {
    using A = AxisHelper<axis>;
    float sum = 0;
    float cross_max = 0;
    for (const auto& c : children) {
        Pt ps = c->preferredSize(dl);
        sum += A::main(ps);
        cross_max = std::max(cross_max, A::cross(ps));
    }
    return A::make_pt(sum, cross_max);
}

/// Perform linear layout: partition space among fixed and flexible children.
template <Axis axis>
void linearLayout(std::vector<std::unique_ptr<Widget>>& children,
                  float available_width, float available_height) {
    using A = AxisHelper<axis>;
    float available_main = A::main_dim(available_width, available_height);
    float available_cross = A::cross_dim(available_width, available_height);

    float fixed_total = 0;
    int flex_count = 0;
    for (const auto& c : children) {
        if (c->isFlexible()) {
            flex_count++;
        } else {
            fixed_total += A::main(c->preferredSize(nullptr));
        }
    }

    float remaining = std::max(0.0f, available_main - fixed_total);
    float flex_size = flex_count > 0 ? remaining / flex_count : 0;

    float pos = 0;
    for (auto& c : children) {
        float child_main = c->isFlexible() ? flex_size : A::main(c->preferredSize(nullptr));
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

} // namespace detail
} // namespace ui

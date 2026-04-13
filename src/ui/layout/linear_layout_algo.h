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
/// Fixed children are summed along main axis; flexible children contribute 0
/// on the main axis (they expand during layout, not during sizing).
/// Cross axis takes the max of all children.
template <Axis axis>
Pt linearPreferredSize(const std::vector<std::unique_ptr<Widget>>& children,
                       IDrawList* dl) {
    using A = AxisHelper<axis>;
    float sum = 0;
    float cross_max = 0;
    for (const auto& c : children) {
        Pt ps = c->preferredSize(dl);
        if (!c->isFlexible()) {
            sum += A::main(ps);
        }
        cross_max = std::max(cross_max, A::cross(ps));
    }
    return A::make_pt(sum, cross_max);
}

/// Compute the minimum shrink size for a linear layout along the given axis.
/// Unlike preferred size, flexible children still contribute their minimum
/// requirement on the main axis so local row/column composition can prevent
/// overlap when a parent is resized smaller.
template <Axis axis>
Pt linearMinimumSize(const std::vector<std::unique_ptr<Widget>>& children,
                     IDrawList* dl) {
    using A = AxisHelper<axis>;
    float sum = 0;
    float cross_max = 0;
    for (const auto& c : children) {
        Pt ms = c->minimumSize(dl);
        sum += A::main(ms);
        cross_max = std::max(cross_max, A::cross(ms));
    }
    return A::make_pt(sum, cross_max);
}

/// Perform linear layout: partition space among fixed and flexible children.
///
/// When there is enough space, non-flex children receive their preferred size
/// and the remainder is distributed to flex children proportionally by flexGrow.
///
/// When space is insufficient for all preferred sizes, non-flex children shrink
/// proportionally from their preferred size toward their minimum size. Flex
/// children receive zero in this case (there is no surplus to distribute).
template <Axis axis>
void linearLayout(std::vector<std::unique_ptr<Widget>>& children,
                  float available_width, float available_height) {
    using A = AxisHelper<axis>;
    float available_main = A::main_dim(available_width, available_height);
    float available_cross = A::cross_dim(available_width, available_height);

    float fixed_preferred_total = 0;
    float fixed_minimum_total = 0;
    float flex_weight_total = 0;
    for (const auto& c : children) {
        if (c->isFlexible()) {
            flex_weight_total += c->flexGrow();
        } else {
            fixed_preferred_total += A::main(c->preferredSize(nullptr));
            fixed_minimum_total += A::main(c->minimumSize(nullptr));
        }
    }

    // Determine whether non-flex children need to shrink.
    const bool needs_shrink = available_main < fixed_preferred_total;
    // How much total shrink budget exists (preferred - minimum across all non-flex).
    const float shrink_budget = std::max(0.0f, fixed_preferred_total - fixed_minimum_total);
    // How much we actually need to shrink to fit.
    const float shrink_needed = std::max(0.0f, fixed_preferred_total - available_main);
    // Fraction of the budget to apply (clamped to 1 = fully shrunk to minimum).
    const float shrink_fraction = (shrink_budget > 0.0f)
        ? std::min(1.0f, shrink_needed / shrink_budget)
        : 0.0f;

    float remaining = std::max(0.0f, available_main - fixed_preferred_total);

    float pos = 0;
    for (auto& c : children) {
        float child_main;
        if (c->isFlexible()) {
            child_main = (flex_weight_total > 0 && !needs_shrink)
                ? remaining * (c->flexGrow() / flex_weight_total)
                : 0;
        } else {
            float pref = A::main(c->preferredSize(nullptr));
            if (needs_shrink) {
                float minv = A::main(c->minimumSize(nullptr));
                child_main = pref - (pref - minv) * shrink_fraction;
            } else {
                child_main = pref;
            }
        }
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

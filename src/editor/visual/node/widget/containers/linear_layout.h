#pragma once

#include "../widget_base.h"
#include <vector>
#include <memory>
#include <algorithm>

/// Axis for LinearLayout: Horizontal stacks children left-to-right,
/// Vertical stacks children top-to-bottom.
enum class Axis { Horizontal, Vertical };

/// Axis-parameterized container that eliminates the Row/Column mirror duplication.
/// - Horizontal: children laid out along X (Row behavior)
/// - Vertical:   children laid out along Y (Column behavior)
template <Axis axis>
class LinearLayout : public Widget {
public:
    Widget* addChild(std::unique_ptr<Widget> child) {
        auto* ptr = child.get();
        children_.push_back(std::move(child));
        return ptr;
    }

    size_t childCount() const { return children_.size(); }

    Widget* child(size_t i) const {
        return i < children_.size() ? children_[i].get() : nullptr;
    }

    Pt getPreferredSize(IDrawList* dl) const override {
        float sum = 0;
        float cross_max = 0;
        for (const auto& c : children_) {
            Pt ps = c->getPreferredSize(dl);
            sum += main(ps);
            cross_max = std::max(cross_max, cross(ps));
        }
        return make_pt(sum, cross_max);
    }

    void layout(float available_width, float available_height) override {
        width_ = available_width;
        height_ = available_height;

        float available_main = main_dim(available_width, available_height);
        float available_cross = cross_dim(available_width, available_height);

        float fixed_total = 0;
        int flex_count = 0;
        for (const auto& c : children_) {
            if (c->isFlexible()) {
                flex_count++;
            } else {
                fixed_total += main(c->getPreferredSize(nullptr));
            }
        }

        float remaining = std::max(0.0f, available_main - fixed_total);
        float flex_size = flex_count > 0 ? remaining / flex_count : 0;

        float pos = 0;
        for (auto& c : children_) {
            float child_main = c->isFlexible() ? flex_size : main(c->getPreferredSize(nullptr));
            set_child_position(*c, pos);
            layout_child(*c, child_main, available_cross);
            pos += child_main;
        }
    }

    void render(IDrawList* dl, Pt origin, float zoom) const override {
        for (const auto& c : children_) {
            Pt child_origin(origin.x + c->x() * zoom, origin.y + c->y() * zoom);
            c->render(dl, child_origin, zoom);
        }
    }

private:
    std::vector<std::unique_ptr<Widget>> children_;

    // Axis helpers - branch resolved at compile time via if constexpr
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
    static void set_child_position(Widget& c, float pos) {
        if constexpr (axis == Axis::Horizontal) c.setPosition(pos, 0);
        else c.setPosition(0, pos);
    }
    static void layout_child(Widget& c, float child_main, float child_cross) {
        if constexpr (axis == Axis::Horizontal) c.layout(child_main, child_cross);
        else c.layout(child_cross, child_main);
    }
};

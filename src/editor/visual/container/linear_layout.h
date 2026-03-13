#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include "ui/layout/linear_layout_algo.h"

namespace visual {

/// Visual-layer linear layout. Reuses the layout algorithm from ui::detail
/// and only adds the context-aware render() for the visual pipeline.
template <ui::Axis axis>
class LinearLayout : public Widget {
public:
    Pt preferredSize(IDrawList* dl) const override {
        return ui::detail::linearPreferredSize<axis>(this->children_, dl);
    }

    void layout(float available_width, float available_height) override {
        setSize(Pt(available_width, available_height));
        ui::detail::linearLayout<axis>(this->children_, available_width, available_height);
    }

    // Rendering handled by Widget::renderTree() — no manual child iteration needed.
    void render(IDrawList* dl, const RenderContext& ctx) const override {}
};

using Row = LinearLayout<ui::Axis::Horizontal>;
using Column = LinearLayout<ui::Axis::Vertical>;

} // namespace visual

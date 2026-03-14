#include "group_node_widget.h"
#include "visual/render_context.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "editor/layout_constants.h"
#include "data/node.h"
#include <algorithm>
#include <cmath>

namespace visual {

// ============================================================================
// Construction
// ============================================================================

GroupNodeWidget::GroupNodeWidget(const ::Node& data, const ui::StringInterner& interner)
    : node_iid_(data.id)
    , interner_(&interner)
    , name_(data.name)
{
    if (data.color.has_value()) {
        custom_fill_ = data.color->to_uint32();
    }

    setLocalPos(data.pos);

    // Snap size to grid, enforce minimums
    auto snap = [](float v) {
        constexpr float g = editor_constants::PORT_LAYOUT_GRID;
        return std::ceil(v / g) * g;
    };
    float w = snap(std::max(data.size.x, editor_constants::MIN_GROUP_WIDTH));
    float h = snap(std::max(data.size.y, editor_constants::MIN_GROUP_HEIGHT));
    setSize(Pt(w, h));
}

// ============================================================================
// Hit testing
// ============================================================================

bool GroupNodeWidget::containsBorder(Pt world_p) const {
    Pt pos = worldPos();
    float x0 = pos.x, y0 = pos.y;
    float x1 = x0 + size().x, y1 = y0 + size().y;

    // Outside bounds entirely
    if (world_p.x < x0 || world_p.x > x1 ||
        world_p.y < y0 || world_p.y > y1)
        return false;

    // Title bar area (always clickable)
    float title_h = editor_constants::GROUP_TITLE_PADDING * 2
                  + editor_constants::Font::Medium;
    if (world_p.y <= y0 + title_h)
        return true;

    // Border margins (left, right, bottom)
    float m = editor_constants::GROUP_BORDER_HIT_MARGIN;
    if (world_p.x <= x0 + m || world_p.x >= x1 - m ||
        world_p.y >= y1 - m)
        return true;

    // Interior — click passes through
    return false;
}

// ============================================================================
// Layout
// ============================================================================

Pt GroupNodeWidget::preferredSize(IDrawList* /*dl*/) const {
    return Pt(editor_constants::MIN_GROUP_WIDTH, editor_constants::MIN_GROUP_HEIGHT);
}

void GroupNodeWidget::layout(float w, float h) {
    setSize(Pt(w, h));
}

// ============================================================================
// Rendering
// ============================================================================

void GroupNodeWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    float zoom = ctx.zoom;

    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::GROUP_ROUNDING * zoom;

    // Semi-transparent fill (use custom color with low alpha if set)
    uint32_t fill;
    if (custom_fill_.has_value()) {
        fill = (custom_fill_.value() & 0x00FFFFFF) | 0x30000000;
    } else {
        fill = render_theme::COLOR_GROUP_FILL;
    }
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    // Title text
    if (!name_.empty()) {
        float pad = editor_constants::GROUP_TITLE_PADDING * zoom;
        Pt text_pos(screen_min.x + pad, screen_min.y + pad);
        dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_GROUP_TITLE,
                     editor_constants::Font::Medium * zoom);
    }

}

void GroupNodeWidget::renderPost(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::GROUP_ROUNDING * ctx.zoom;

    // Selection border drawn after children so it appears on top
    handle_renderer::draw_selection_border(*dl, ctx, *this, screen_min, screen_max, rounding);

    // Resize handles (drawn when selected)
    handle_renderer::draw_resize_handles(*dl, ctx, *this);
}

} // namespace visual

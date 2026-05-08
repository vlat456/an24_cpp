#include "group_node_widget.h"
#include "editor/visual/presentation/hit_geometry.h"
#include "visual/render_context.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/snap.h"
#include "editor/layout_constants.h"
#include "data/node_content.h"
#include "blueprint_v2/blueprint/blueprint.h"
#include <algorithm>
#include <cmath>

namespace visual {

// ============================================================================
// Construction
// ============================================================================

GroupNodeWidget::GroupNodeWidget(const bp2::Blueprint::Node& data,
                                 const core::StringInterner& interner,
                                 std::optional<editor::NodeColor> color)
    : node_iid_(data.semantic.id)
    , interner_(&interner)
    , name_(data.view.name)
{
    kind_ = ui::WidgetKind::GroupNode;
    if (color.has_value()) {
        custom_fill_ = color->to_uint32();
    }

    setLocalPos(Pt(data.layout.x, data.layout.y));

    // Snap size to grid, enforce minimums
    float const sw = data.layout.width.has_value()  ? *data.layout.width  : editor_constants::MIN_GROUP_WIDTH;
    float const sh = data.layout.height.has_value() ? *data.layout.height : editor_constants::MIN_GROUP_HEIGHT;
    float const w = editor_math::snap_size_to_layout_grid(std::max(sw, editor_constants::MIN_GROUP_WIDTH));
    float const h = editor_math::snap_size_to_layout_grid(std::max(sh, editor_constants::MIN_GROUP_HEIGHT));
    setSize(Pt(w, h));
}

// ============================================================================
// Hit testing
// ============================================================================

bool GroupNodeWidget::containsBorder(Pt world_p) const {
    return editor::presentation::hit_geometry::point_hits_group_frame(
        world_p,
        ui::Rect{worldPos().x, worldPos().y, size().x, size().y});
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

    Pt const pos = worldPos();
    Pt const sz = size();
    float const zoom = ctx.zoom;

    Pt const screen_min = ctx.world_to_screen(pos);
    Pt const screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float const rounding = editor_constants::GROUP_ROUNDING * zoom;

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
        float const pad = editor_constants::GROUP_TITLE_PADDING * zoom;
        Pt const text_pos(screen_min.x + pad, screen_min.y + pad);
        dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_GROUP_TITLE,
                     editor_constants::Font::Medium * zoom);
    }

}

void GroupNodeWidget::renderPost(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt const pos = worldPos();
    Pt const sz = size();
    Pt const screen_min = ctx.world_to_screen(pos);
    Pt const screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float const rounding = editor_constants::GROUP_ROUNDING * ctx.zoom;

    // Selection border drawn after children so it appears on top
    handle_renderer::draw_selection_border(*dl, ctx, *this, screen_min, screen_max, rounding);

    // Resize handles (drawn when selected)
    handle_renderer::draw_resize_handles(*dl, ctx, *this);
}

} // namespace visual

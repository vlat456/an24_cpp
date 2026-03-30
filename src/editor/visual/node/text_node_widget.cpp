#include "text_node_widget.h"
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

TextNodeWidget::TextNodeWidget(const bp2::Blueprint::Node& data, const ui::StringInterner& interner)
    : node_iid_(data.id)
    , interner_(&interner)
    , name_(data.name)
    , font_size_base_(editor_constants::Font::Large)
{
    if (data.has_color) {
        NodeColor c;
        c.r = data.color_r;
        c.g = data.color_g;
        c.b = data.color_b;
        c.a = data.color_a;
        custom_fill_ = c.to_uint32();
    }

    setLocalPos(Pt(data.x, data.y));

    // Extract text from string_params
    {
        auto it = data.string_params.find("text");
        if (it != data.string_params.end()) {
            text_ = it->second;
        }
    }

    // Extract font size from string_params
    {
        auto fs = data.string_params.find("font_size");
        if (fs != data.string_params.end()) {
            if (fs->second == "small")       font_size_base_ = editor_constants::Font::Small;
            else if (fs->second == "medium") font_size_base_ = editor_constants::Font::Medium;
        }
    }

    // Snap size to grid
    ui::Pt default_sz(128.0f, 48.0f);
    float sw = data.width.has_value()  ? *data.width  : default_sz.x;
    float sh = data.height.has_value() ? *data.height : default_sz.y;
    float w = editor_math::snap_size_to_layout_grid(std::max(sw, 64.0f));
    float h = editor_math::snap_size_to_layout_grid(std::max(sh, 32.0f));
    setSize(Pt(w, h));
}

// ============================================================================
// Layout
// ============================================================================

Pt TextNodeWidget::preferredSize(IDrawList* /*dl*/) const {
    return Pt(128.0f, 48.0f);
}

void TextNodeWidget::layout(float w, float h) {
    setSize(Pt(w, h));
}

// ============================================================================
// Rendering
// ============================================================================

void TextNodeWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;

    Pt pos = worldPos();
    Pt sz = size();
    float zoom = ctx.zoom;

    Pt screen_min = ctx.world_to_screen(pos);
    Pt screen_max = ctx.world_to_screen(Pt(pos.x + sz.x, pos.y + sz.y));
    float rounding = editor_constants::GROUP_ROUNDING * zoom;

    // Border only (no fill for text nodes)
    uint32_t border_color = render_theme::COLOR_TEXT_BORDER;
    dl->add_rect_with_rounding_corners(screen_min, screen_max, border_color, rounding,
                                       editor_constants::DRAW_CORNERS_ALL, 1.0f);

    float pad = editor_constants::GROUP_TITLE_PADDING * zoom;
    float font_size = font_size_base_ * zoom;

    if (text_.empty()) {
        // Placeholder
        dl->add_text(Pt(screen_min.x + pad, screen_min.y + pad),
                     "Text", render_theme::COLOR_TEXT_DIM, font_size);
    } else {
        // Multiline text rendering
        float line_height = font_size * 1.4f;
        float y = screen_min.y + pad;

        size_t p = 0;
        while (p < text_.size()) {
            size_t nl = text_.find('\n', p);
            if (nl == std::string::npos) nl = text_.size();
            std::string line = text_.substr(p, nl - p);
            if (!line.empty()) {
                dl->add_text(Pt(screen_min.x + pad, y), line.c_str(),
                             render_theme::COLOR_TEXT, font_size);
            }
            y += line_height;
            p = nl + 1;
        }
    }

}

void TextNodeWidget::renderPost(IDrawList* dl, const RenderContext& ctx) const {
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

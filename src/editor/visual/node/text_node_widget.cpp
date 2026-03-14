#include "text_node_widget.h"
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

TextNodeWidget::TextNodeWidget(const ::Node& data, const ui::StringInterner& interner)
    : node_id_(interner.resolve(data.id))
    , name_(data.name)
    , font_size_base_(editor_constants::Font::Large)
{
    if (data.color.has_value()) {
        custom_fill_ = data.color->to_uint32();
    }

    setLocalPos(data.pos);

    // Extract text from params
    auto it = data.params.find("text");
    if (it != data.params.end()) {
        text_ = it->second;
    }

    // Extract font size from params
    auto fs = data.params.find("font_size");
    if (fs != data.params.end()) {
        if (fs->second == "small")       font_size_base_ = editor_constants::Font::Small;
        else if (fs->second == "medium") font_size_base_ = editor_constants::Font::Medium;
    }

    // Snap size to grid
    auto snap = [](float v) {
        constexpr float g = editor_constants::PORT_LAYOUT_GRID;
        return std::ceil(v / g) * g;
    };
    float w = snap(std::max(data.size.x, 64.0f));
    float h = snap(std::max(data.size.y, 32.0f));
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

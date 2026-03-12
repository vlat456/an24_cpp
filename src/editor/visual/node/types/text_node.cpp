#include "text_node.h"
#include "editor/visual/node/node_utils.h"
#include "editor/visual/renderer/render_theme.h"
#include "editor/visual/renderer/draw_list.h"
#include "editor/layout_constants.h"


TextVisualNode::TextVisualNode(const Node& node)
    : VisualNode(node)
{
    size_ = node_utils::snap_size_to_grid(node.size);
    ports_.clear();
    port_slots_.clear();

    auto it = node.params.find("text");
    if (it != node.params.end())
        text_ = it->second;

    font_size_base_ = editor_constants::Font::Large;
    auto fs = node.params.find("font_size");
    if (fs != node.params.end()) {
        if (fs->second == "small")       font_size_base_ = editor_constants::Font::Small;
        else if (fs->second == "medium") font_size_base_ = editor_constants::Font::Medium;
    }
}

void TextVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                            bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    float rounding = editor_constants::GROUP_ROUNDING * vp.zoom;

    uint32_t border_color = is_selected
        ? render_theme::COLOR_SELECTED
        : render_theme::COLOR_TEXT_BORDER;
    dl->add_rect_with_rounding_corners(screen_min, screen_max, border_color, rounding,
                                       editor_constants::DRAW_CORNERS_ALL, 1.0f);

    float pad = editor_constants::GROUP_TITLE_PADDING * vp.zoom;
    float font_size = font_size_base_ * vp.zoom;

    if (text_.empty()) {
        dl->add_text(Pt(screen_min.x + pad, screen_min.y + pad),
                     "Text", render_theme::COLOR_TEXT_DIM, font_size);
    } else {
        float line_height = font_size * 1.4f;
        float y = screen_min.y + pad;

        size_t pos = 0;
        while (pos < text_.size()) {
            size_t nl = text_.find('\n', pos);
            if (nl == std::string::npos) nl = text_.size();
            std::string line = text_.substr(pos, nl - pos);
            if (!line.empty()) {
                dl->add_text(Pt(screen_min.x + pad, y), line.c_str(),
                             render_theme::COLOR_TEXT, font_size);
            }
            y += line_height;
            pos = nl + 1;
        }
    }
}


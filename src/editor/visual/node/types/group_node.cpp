#include "group_node.h"
#include "editor/visual/node/node_utils.h"
#include "editor/visual/renderer/render_theme.h"
#include "editor/visual/renderer/draw_list.h"
#include "editor/layout_constants.h"

namespace an24 {

GroupVisualNode::GroupVisualNode(const Node& node)
    : VisualNode(node)
{
    size_ = node_utils::snap_size_to_grid(node.size);
    ports_.clear();
    port_slots_.clear();
}

bool GroupVisualNode::containsPoint(Pt world_pos) const {
    float x0 = position_.x, y0 = position_.y;
    float x1 = x0 + size_.x, y1 = y0 + size_.y;

    if (world_pos.x < x0 || world_pos.x > x1 ||
        world_pos.y < y0 || world_pos.y > y1)
        return false;

    float m = editor_constants::GROUP_BORDER_HIT_MARGIN;

    float title_h = editor_constants::GROUP_TITLE_PADDING * 2 + editor_constants::Font::Medium;
    if (world_pos.y <= y0 + title_h)
        return true;

    if (world_pos.x <= x0 + m || world_pos.x >= x1 - m ||
        world_pos.y >= y1 - m)
        return true;

    return false;
}

void GroupVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                             bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    float rounding = editor_constants::GROUP_ROUNDING * vp.zoom;

    uint32_t fill = custom_color_.has_value()
        ? (custom_color_->to_uint32() & 0x00FFFFFF) | 0x30000000
        : render_theme::COLOR_GROUP_FILL;
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    if (is_selected) {
        dl->add_rect_with_rounding_corners(screen_min, screen_max,
                                           render_theme::COLOR_SELECTED, rounding,
                                           editor_constants::DRAW_CORNERS_ALL, 1.0f);
    }

    if (!name_.empty()) {
        float pad = editor_constants::GROUP_TITLE_PADDING * vp.zoom;
        Pt text_pos(screen_min.x + pad, screen_min.y + pad);
        dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_GROUP_TITLE,
                     editor_constants::Font::Medium * vp.zoom);
    }
}

} // namespace an24

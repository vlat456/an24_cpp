#include "ref_node.h"
#include "editor/visual/node/node_utils.h"
#include "editor/visual/renderer/render_theme.h"
#include "editor/visual/renderer/draw_list.h"
#include "editor/layout_constants.h"


RefVisualNode::RefVisualNode(const Node& node)
    : VisualNode(node)
{
    size_ = node_utils::snap_size_to_grid(node.size);
    ports_.clear();

    std::string port_name = "v";
    PortType port_type = PortType::V;
    if (!node.outputs.empty()) {
        port_name = node.outputs[0].name;
        port_type = node.outputs[0].type;
    } else if (!node.inputs.empty()) {
        port_name = node.inputs[0].name;
        port_type = node.inputs[0].type;
    }

    VisualPort vp(port_name, PortSide::Output, port_type);
    vp.setWorldPosition(node_utils::snap_to_grid(Pt(position_.x + size_.x / 2, position_.y)));
    ports_.push_back(std::move(vp));
}

void RefVisualNode::recalculatePorts() {
    if (!ports_.empty()) {
        ports_[0].setWorldPosition(node_utils::snap_to_grid(Pt(position_.x + size_.x / 2, position_.y)));
    }
}

void RefVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                          bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);
    Pt screen_center((screen_min.x + screen_max.x) / 2,
                     (screen_min.y + screen_max.y) / 2);

    uint32_t fill = custom_color_.has_value()
        ? custom_color_->to_uint32()
        : render_theme::COLOR_BUS_FILL;
    float rounding = editor_constants::NODE_ROUNDING * vp.zoom;
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    Pt text_pos(screen_min.x + 2 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, 10.0f * vp.zoom);

    uint32_t border_color = is_selected ? render_theme::COLOR_SELECTED : render_theme::COLOR_BUS_BORDER;
    dl->add_rect_with_rounding_corners(screen_min, screen_max, border_color, rounding, editor_constants::DRAW_CORNERS_ALL, 1.0f);

    Pt world_port_pos = node_utils::snap_to_grid(Pt(position_.x + size_.x / 2, position_.y));
    Pt port_pos = vp.world_to_screen(world_port_pos, canvas_min);
    uint32_t port_color = render_theme::get_port_color(ports_[0].type());
    dl->add_circle_filled(port_pos, editor_constants::PORT_RADIUS * vp.zoom, port_color, 8);
}


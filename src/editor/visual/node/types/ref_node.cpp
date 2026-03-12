#include "ref_node.h"
#include "editor/visual/node/node_utils.h"
#include "editor/visual/renderer/node_frame.h"
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
    if (ports_.empty()) return;

    auto bounds = node_frame::world_to_screen(position_, size_, vp, canvas_min);
    Pt screen_center = bounds.center();
    float rounding = editor_constants::NODE_ROUNDING * vp.zoom;

    uint32_t fill = node_frame::get_fill_color(customColor(), render_theme::COLOR_BUS_FILL);
    node_frame::render_fill(*dl, bounds, rounding, fill);

    Pt text_pos(bounds.min.x + 2 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, 10.0f * vp.zoom);

    node_frame::render_border(*dl, bounds, rounding, is_selected);

    Pt world_port_pos = node_utils::snap_to_grid(Pt(position_.x + size_.x / 2, position_.y));
    Pt port_pos = vp.world_to_screen(world_port_pos, canvas_min);
    uint32_t port_color = render_theme::get_port_color(ports_[0].type());
    dl->add_circle_filled(port_pos, editor_constants::PORT_RADIUS * vp.zoom, port_color, 8);
}

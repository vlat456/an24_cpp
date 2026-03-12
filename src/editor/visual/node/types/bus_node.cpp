#include "bus_node.h"
#include "editor/visual/node/node_utils.h"
#include "editor/visual/renderer/render_theme.h"
#include "editor/visual/renderer/draw_list.h"
#include "editor/layout_constants.h"
#include <algorithm>
#include <cassert>


BusVisualNode::BusVisualNode(const Node& node, BusOrientation orientation,
                             const std::vector<Wire>& wires)
    : VisualNode(node)
    , orientation_(orientation)
{
    for (const auto& w : wires) {
        if (w.start.node_id == node_id_ || w.end.node_id == node_id_)
            wires_.push_back(w);
    }
    size_ = node_utils::snap_size_to_grid(node.size);
    distributePortsInRow(wires_);
}

void BusVisualNode::distributePortsInRow(const std::vector<Wire>& wires) {
    ports_.clear();

    for (const auto& w : wires) {
        if (w.start.node_id == node_id_ || w.end.node_id == node_id_) {
            VisualPort vp(w.id, PortSide::InOut, PortType::V, "v");
            vp.setWorldPosition(calculatePortPosition(ports_.size()));
            ports_.push_back(std::move(vp));
        }
    }

    VisualPort v_port("v", PortSide::InOut, PortType::V);
    v_port.setWorldPosition(calculatePortPosition(ports_.size()));
    ports_.push_back(std::move(v_port));

    if (!wires.empty()) {
        size_ = calculateBusSize(ports_.size());
    }

    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i].setWorldPosition(calculatePortPosition(i));
    }

    assert(ports_.size() == wires_.size() + 1);
}

Pt BusVisualNode::calculateBusSize(size_t port_count) const {
    Pt size;
    if (orientation_ == BusOrientation::Horizontal) {
        size = Pt((port_count + 2) * editor_constants::PORT_LAYOUT_GRID, editor_constants::PORT_LAYOUT_GRID * 2);
    } else {
        size = Pt(editor_constants::PORT_LAYOUT_GRID * 2, (port_count + 2) * editor_constants::PORT_LAYOUT_GRID);
    }
    return node_utils::snap_size_to_grid(size);
}

Pt BusVisualNode::calculatePortPosition(size_t index) const {
    if (ports_.empty() && index == 0) {
        return node_utils::snap_to_grid(Pt(position_.x + size_.x / 2, position_.y + size_.y / 2));
    }

    bool ports_on_bottom = (size_.x > size_.y);
    float step = editor_constants::PORT_LAYOUT_GRID;

    if (ports_on_bottom) {
        float x = position_.x + step * (index + 1);
        float y = position_.y + size_.y;
        return node_utils::snap_to_grid(Pt(x, y));
    } else {
        float x = position_.x + size_.x;
        float y = position_.y + step * (index + 1);
        return node_utils::snap_to_grid(Pt(x, y));
    }
}

const VisualPort* BusVisualNode::resolveWirePort(const std::string& port_name,
                                                  const char* wire_id) const {
    if (port_name == "v" && wire_id != nullptr) {
        for (const auto& p : ports_) {
            if (p.name() == wire_id) return &p;
        }
    }
    return getPort(port_name);
}

void BusVisualNode::connectWire(const Wire& wire) {
    if (wire.start.node_id == node_id_ || wire.end.node_id == node_id_) {
        wires_.push_back(wire);
        distributePortsInRow(wires_);
        size_ = calculateBusSize(ports_.size());
    }
}

void BusVisualNode::disconnectWire(const Wire& wire) {
    wires_.erase(
        std::remove_if(wires_.begin(), wires_.end(),
            [&](const Wire& w) { return w.id == wire.id; }),
        wires_.end());
    std::string port_name = wire.id;
    ports_.erase(
        std::remove_if(ports_.begin(), ports_.end(),
            [&](const VisualPort& p) { return p.name() == port_name; }),
        ports_.end());
    size_ = calculateBusSize(ports_.size());
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i].setWorldPosition(calculatePortPosition(i));
    }
    assert(ports_.size() == wires_.size() + 1);
}

void BusVisualNode::recalculatePorts() {
    distributePortsInRow(wires_);
}

bool BusVisualNode::handlePortSwap(const std::string& port_a,
                                  const std::string& port_b) {
    if (port_a.empty() || port_b.empty())
        return false;
    return swapAliasPorts(port_a, port_b);
}

bool BusVisualNode::swapAliasPorts(const std::string& wire_id_a,
                                   const std::string& wire_id_b) {
    size_t idx_a = SIZE_MAX, idx_b = SIZE_MAX;
    for (size_t i = 0; i < wires_.size(); i++) {
        if (wires_[i].id == wire_id_a) idx_a = i;
        if (wires_[i].id == wire_id_b) idx_b = i;
    }

    if (idx_a == SIZE_MAX || idx_b == SIZE_MAX || idx_a == idx_b)
        return false;

    std::swap(wires_[idx_a], wires_[idx_b]);
    distributePortsInRow(wires_);
    size_ = calculateBusSize(ports_.size());

    return true;
}

void BusVisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                          bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);
    Pt screen_center((screen_min.x + screen_max.x) / 2,
                     (screen_min.y + screen_max.y) / 2);

    float bus_w = size_.x * vp.zoom;
    float bus_h = size_.y * vp.zoom;
    Pt bus_min(screen_center.x - bus_w / 2, screen_center.y - bus_h / 2);
    Pt bus_max(screen_center.x + bus_w / 2, screen_center.y + bus_h / 2);

    uint32_t fill = custom_color_.has_value()
        ? custom_color_->to_uint32()
        : render_theme::COLOR_BUS_FILL;
    float rounding = editor_constants::NODE_ROUNDING * vp.zoom;
    dl->add_rect_filled_with_rounding(bus_min, bus_max, fill, rounding);

    Pt text_pos(bus_min.x + 3 * vp.zoom, screen_center.y - 5 * vp.zoom);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, 10.0f * vp.zoom);

    uint32_t border_color = is_selected ? render_theme::COLOR_SELECTED : render_theme::COLOR_BUS_BORDER;
    dl->add_rect_with_rounding_corners(bus_min, bus_max, border_color, rounding, editor_constants::DRAW_CORNERS_ALL, 1.0f);

    float port_radius = editor_constants::PORT_RADIUS * vp.zoom;
    for (const auto& port : ports_) {
        Pt screen_pos = vp.world_to_screen(port.worldPosition(), canvas_min);
        uint32_t port_color = render_theme::get_port_color(port.type());
        dl->add_circle_filled(screen_pos, port_radius, port_color, 8);
    }
}


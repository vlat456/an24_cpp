#include "visual/node/node.h"
#include "visual/node/widget.h"
#include "data/node.h"
#include "visual/node/node_utils.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/draw_list.h"
#include "layout_constants.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>


#ifndef EDITOR_TESTING
#include <imgui.h>
#endif

VisualNode::VisualNode(const Node& node)
    : position_(node.pos)
    , size_(node.size)
    , node_id_(node.id)
    , ports_()
    , name_(node.name)
    , type_name_(node.type_name)
    , node_content_(node.node_content)
    , custom_color_(node.color)
{
    buildLayout(node);

    Pt preferred = layout_.getPreferredSize(nullptr);
    preferred.x = std::max(preferred.x, editor_constants::MIN_NODE_WIDTH);

    bool has_explicit_width = node.size_explicitly_set;
    bool has_explicit_height = node.size_explicitly_set;

    if (node.node_content.type == NodeContentType::Gauge) {
        spdlog::info("[autosize] GAUGE Node '{}': JSON size={:.1f}x{:.1f}, explicit={}/{}, preferred={:.1f}x{:.1f}",
                     node.name, node.size.x, node.size.y,
                     has_explicit_width, has_explicit_height, preferred.x, preferred.y);
    }

    if (!has_explicit_width && !has_explicit_height) {
        size_ = node_utils::snap_size_to_grid(preferred);
        spdlog::debug("[autosize] Node '{}' auto-sized to {:.1f}x{:.1f}",
                      node.name, size_.x, size_.y);
    } else {
        if (!has_explicit_width) {
            size_.x = node_utils::snap_size_to_grid(Pt(preferred.x, 0)).x;
        } else if (node.size.x < preferred.x) {
            spdlog::warn("[autosize] Node '{}' explicit width {:.1f}px is too small "
                         "(minimum required: {:.1f}px). Content may be clipped.",
                         node.name, node.size.x, preferred.x);
        } else {
            size_.x = node.size.x;
        }

        if (!has_explicit_height) {
            size_.y = node_utils::snap_size_to_grid(Pt(0, preferred.y)).y;
        } else if (node.size.y < preferred.y) {
            spdlog::warn("[autosize] Node '{}' explicit height {:.1f}px is too small "
                         "(minimum required: {:.1f}px). Content may be clipped.",
                         node.name, node.size.y, preferred.y);
        } else {
            size_.y = node.size.y;
        }
    }

    layout_.layout(size_.x, size_.y);

    if (node_content_.type == NodeContentType::VerticalToggle && content_widget_ && layout_.childCount() > 1) {
        Widget* main_row = layout_.child(1);
        auto* row_ptr = dynamic_cast<Row*>(main_row);
        if (row_ptr && row_ptr->childCount() > 1) {
            Widget* center_col = row_ptr->child(1);
            content_offset_ = Pt(main_row->x() + center_col->x(),
                                 main_row->y() + center_col->y());
        }
    }

    buildPorts(node);
}

void VisualNode::setPosition(Pt pos) {
    Pt delta(pos.x - position_.x, pos.y - position_.y);
    position_ = pos;
    for (auto& port : ports_) {
        Pt old = port.worldPosition();
        port.setWorldPosition(Pt(old.x + delta.x, old.y + delta.y));
    }
}

void VisualNode::setSize(Pt size) {
    size_ = node_utils::snap_size_to_grid(size);
}

void VisualNode::buildLayout(const Node& node) {
    layout_.addChild(std::make_unique<HeaderWidget>(
        name_, render_theme::COLOR_HEADER_FILL, editor_constants::NODE_ROUNDING));

    port_slots_.clear();

    if (node_content_.type == NodeContentType::VerticalToggle) {
        auto main_row = std::make_unique<Row>();

        auto left_col = std::make_unique<Column>();
        for (size_t i = 0; i < node.inputs.size(); i++) {
            const auto& port = node.inputs[i];
            auto label = std::make_unique<Label>(port.name, editor_constants::PORT_LABEL_FONT_SIZE, editor_constants::PORT_LABEL_COLOR);
            float inner_h = label->getPreferredSize(nullptr).y;
            float pad = std::max(0.0f, (editor_constants::PORT_ROW_HEIGHT - inner_h) / 2.0f);
            auto row_container = std::make_unique<Container>(
                std::move(label),
                Edges{editor_constants::PORT_RADIUS + editor_constants::PORT_LABEL_GAP, pad, 0, pad}
            );
            auto* ptr = left_col->addChild(std::move(row_container));
            port_slots_.push_back({ptr, port.name, true, port.type, 0});
        }

        auto center_col = std::make_unique<Column>();
        auto vt = std::make_unique<VerticalToggleWidget>(node_content_.state, node_content_.tripped);
        auto toggle_container = std::make_unique<Container>(
            std::move(vt), Edges{0, 5.0f, 0, 5.0f}
        );
        toggle_container->setFlexible(true);
        content_widget_ = center_col->addChild(std::move(toggle_container));
        center_col->setFlexible(true);

        auto right_col = std::make_unique<Column>();
        for (size_t i = 0; i < node.outputs.size(); i++) {
            const auto& port = node.outputs[i];
            auto label = std::make_unique<Label>(port.name, editor_constants::PORT_LABEL_FONT_SIZE,
                                                 editor_constants::PORT_LABEL_COLOR, TextAlign::Right);
            float inner_h = label->getPreferredSize(nullptr).y;
            float pad = std::max(0.0f, (editor_constants::PORT_ROW_HEIGHT - inner_h) / 2.0f);
            auto row_container = std::make_unique<Container>(
                std::move(label),
                Edges{0, pad, editor_constants::PORT_RADIUS + editor_constants::PORT_LABEL_GAP, pad}
            );
            auto* ptr = right_col->addChild(std::move(row_container));
            port_slots_.push_back({ptr, port.name, false, port.type, 0});
        }

        main_row->addChild(std::move(left_col));
        main_row->addChild(std::move(center_col));
        main_row->addChild(std::move(right_col));
        main_row->setFlexible(true);

        auto* main_row_ptr = layout_.addChild(std::move(main_row));
        for (auto& slot : port_slots_) {
            slot.parent_y_offset = -1.0f;
        }
        (void)main_row_ptr;
    } else {
        size_t max_ports = std::max(node.inputs.size(), node.outputs.size());
        for (size_t i = 0; i < max_ports; i++) {
            std::string left_name = (i < node.inputs.size()) ? node.inputs[i].name : "";
            std::string right_name = (i < node.outputs.size()) ? node.outputs[i].name : "";
            PortType left_type = (i < node.inputs.size()) ? node.inputs[i].type : PortType::Any;
            PortType right_type = (i < node.outputs.size()) ? node.outputs[i].type : PortType::Any;

            auto row = std::make_unique<Row>();

            if (!left_name.empty()) {
                uint32_t left_color = render_theme::get_port_color(left_type);
                row->addChild(std::make_unique<Container>(
                    std::make_unique<Circle>(editor_constants::PORT_RADIUS, left_color),
                    Edges{-editor_constants::PORT_RADIUS, 0, editor_constants::PORT_LABEL_GAP, 0}
                ));
                row->addChild(std::make_unique<Label>(left_name, editor_constants::PORT_LABEL_FONT_SIZE, editor_constants::PORT_LABEL_COLOR));
            }

            if (!left_name.empty() && !right_name.empty()) {
                float half_gap = editor_constants::PORT_MIN_GAP / 2.0f;
                auto gap = std::make_unique<Container>(
                    std::make_unique<Spacer>(),
                    Edges{half_gap, 0, half_gap, 0}
                );
                gap->setFlexible(true);
                row->addChild(std::move(gap));
            } else {
                row->addChild(std::make_unique<Spacer>());
            }

            if (!right_name.empty()) {
                row->addChild(std::make_unique<Label>(right_name, editor_constants::PORT_LABEL_FONT_SIZE, editor_constants::PORT_LABEL_COLOR));
                uint32_t right_color = render_theme::get_port_color(right_type);
                row->addChild(std::make_unique<Container>(
                    std::make_unique<Circle>(editor_constants::PORT_RADIUS, right_color),
                    Edges{editor_constants::PORT_LABEL_GAP, 0, -editor_constants::PORT_RADIUS, 0}
                ));
            }

            float inner_h = row->getPreferredSize(nullptr).y;
            float pad = std::max(0.0f, (editor_constants::PORT_ROW_HEIGHT - inner_h) / 2.0f);
            auto row_container = std::make_unique<Container>(
                std::move(row),
                Edges{0, pad, 0, pad}
            );

            auto* container_ptr = layout_.addChild(std::move(row_container));

            if (!left_name.empty()) {
                port_slots_.push_back({container_ptr, left_name, true, left_type, 0});
            }
            if (!right_name.empty()) {
                port_slots_.push_back({container_ptr, right_name, false, right_type, 0});
            }
        }

        if (node_content_.type != NodeContentType::None) {
            if (node_content_.type == NodeContentType::Gauge) {
                layout_.addChild(std::make_unique<VoltmeterWidget>(
                    node_content_.value,
                    node_content_.min,
                    node_content_.max,
                    node_content_.unit
                ));
            } else if (node_content_.type == NodeContentType::Switch) {
                float margin = editor_constants::PORT_RADIUS + editor_constants::PORT_LABEL_GAP;
                float v_pad = 2.0f;
                auto sw = std::make_unique<SwitchWidget>(node_content_.state, node_content_.tripped);
                auto content_container = std::make_unique<Container>(
                    std::move(sw),
                    Edges{margin, v_pad, margin, v_pad}
                );
                content_container->setFlexible(true);
                content_widget_ = layout_.addChild(std::move(content_container));
            } else {
                float margin = editor_constants::PORT_RADIUS + editor_constants::PORT_LABEL_GAP;
                std::unique_ptr<Widget> content_inner;
                if (!node_content_.label.empty()) {
                    content_inner = std::make_unique<Label>(node_content_.label, 10.0f, 0x00000000);
                } else {
                    content_inner = std::make_unique<Spacer>();
                }
                auto content_container = std::make_unique<Container>(
                    std::move(content_inner),
                    Edges{margin, 0, margin, 0}
                );
                content_container->setFlexible(true);
                content_widget_ = layout_.addChild(std::move(content_container));
            }
        }
    }

    layout_.addChild(std::make_unique<TypeNameWidget>(type_name_));
}

void VisualNode::buildPorts(const Node& node) {
    ports_.clear();

    if (node_content_.type == NodeContentType::VerticalToggle && layout_.childCount() > 1) {
        float main_row_y = layout_.child(1)->y();
        for (auto& slot : port_slots_) {
            if (slot.parent_y_offset < 0)
                slot.parent_y_offset = main_row_y;
        }
    }

    for (const auto& p : node.inputs) {
        VisualPort vp(p.name, PortSide::Input, p.type);
        for (const auto& slot : port_slots_) {
            if (slot.is_left && slot.name == p.name) {
                float port_y = position_.y + slot.parent_y_offset + slot.row_container->y() + slot.row_container->height() / 2;
                float port_x = position_.x;
                vp.setWorldPosition(Pt(port_x, port_y));
                break;
            }
        }
        ports_.push_back(std::move(vp));
    }
    for (const auto& p : node.outputs) {
        VisualPort vp(p.name, PortSide::Output, p.type);
        for (const auto& slot : port_slots_) {
            if (!slot.is_left && slot.name == p.name) {
                float port_y = position_.y + slot.parent_y_offset + slot.row_container->y() + slot.row_container->height() / 2;
                float port_x = position_.x + size_.x;
                vp.setWorldPosition(Pt(port_x, port_y));
                break;
            }
        }
        ports_.push_back(std::move(vp));
    }
}

const VisualPort* VisualNode::getPort(const std::string& name) const {
    for (const auto& p : ports_) {
        if (p.name() == name) return &p;
    }
    return nullptr;
}

const VisualPort* VisualNode::getPort(size_t index) const {
    return index < ports_.size() ? &ports_[index] : nullptr;
}

std::vector<std::string> VisualNode::getPortNames() const {
    std::vector<std::string> names;
    for (const auto& p : ports_) names.push_back(p.name());
    return names;
}

const VisualPort* VisualNode::resolveWirePort(const std::string& port_name,
                                               const char* wire_id) const {
    (void)wire_id;
    return getPort(port_name);
}

void VisualNode::connectWire(const Wire&) {}
void VisualNode::disconnectWire(const Wire&) {}
void VisualNode::recalculatePorts() {}

void VisualNode::updateNodeContent(const NodeContent& content) {
    node_content_ = content;
    if (node_content_.type == NodeContentType::Gauge) {
        for (size_t i = 0; i < layout_.childCount(); i++) {
            if (auto* vw = dynamic_cast<VoltmeterWidget*>(layout_.child(i))) {
                vw->setValue(node_content_.value);
                break;
            }
        }
    }
    if (node_content_.type == NodeContentType::Switch) {
        if (auto* c = dynamic_cast<Container*>(content_widget_)) {
            if (auto* sw = dynamic_cast<SwitchWidget*>(c->child())) {
                sw->setState(node_content_.state);
                sw->setTripped(node_content_.tripped);
            }
        }
    }
    if (node_content_.type == NodeContentType::VerticalToggle) {
        if (auto* c = dynamic_cast<Container*>(content_widget_)) {
            if (auto* vt = dynamic_cast<VerticalToggleWidget*>(c->child())) {
                vt->setState(node_content_.state);
                vt->setTripped(node_content_.tripped);
            }
        }
    }
}

void VisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                        bool is_selected) const {
    Pt screen_min = vp.world_to_screen(position_, canvas_min);
    Pt screen_max = vp.world_to_screen(
        Pt(position_.x + size_.x, position_.y + size_.y), canvas_min);

    float rounding = editor_constants::NODE_ROUNDING * vp.zoom;
    uint32_t border_color = is_selected ? render_theme::COLOR_SELECTED : render_theme::COLOR_BUS_BORDER;

    uint32_t fill = custom_color_.has_value()
        ? custom_color_->to_uint32()
        : render_theme::COLOR_BODY_FILL;
    dl->add_rect_filled_with_rounding(screen_min, screen_max, fill, rounding);

    layout_.render(dl, screen_min, vp.zoom);

    dl->add_rect_with_rounding_corners(
        screen_min, screen_max, border_color, rounding,
        editor_constants::DRAW_CORNERS_ALL, 1.0f);

    float port_radius = editor_constants::PORT_RADIUS * vp.zoom;
    for (const auto& port : ports_) {
        Pt screen_pos = vp.world_to_screen(port.worldPosition(), canvas_min);
        uint32_t port_color = render_theme::get_port_color(port.type());
        dl->add_circle_filled(screen_pos, port_radius, port_color, 8);
    }
}

Bounds VisualNode::getContentBounds() const {
    if (!content_widget_) return {};

    auto* container = dynamic_cast<const Container*>(content_widget_);
    if (!container || !container->child()) return {};

    const Widget* child = container->child();
    return {
        content_offset_.x + content_widget_->x() + child->x(),
        content_offset_.y + content_widget_->y() + child->y(),
        child->width(),
        child->height()
    };
}

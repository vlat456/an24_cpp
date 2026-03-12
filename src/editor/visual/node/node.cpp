#include "visual/node/node.h"
#include "visual/node/edges.h"
#include "visual/node/widget/containers/column.h"
#include "visual/node/widget/containers/row.h"
#include "visual/node/widget/containers/container.h"
#include "visual/node/widget/primitives/label.h"
#include "visual/node/widget/primitives/spacer.h"
#include "visual/node/widget/content/header_widget.h"
#include "visual/node/widget/content/type_name_widget.h"
#include "visual/node/widget/content/switch_widget.h"
#include "visual/node/widget/content/vertical_toggle.h"
#include "visual/node/widget/content/voltmeter_widget.h"
#include "visual/renderer/node_frame.h"
#include "visual/renderer/port_layout_builder.h"
#include "visual/renderer/render_theme.h"
#include "data/node.h"
#include "visual/node/node_utils.h"
#include "visual/renderer/draw_list.h"
#include "editor/layout_constants.h"
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

    std::vector<port_layout_builder::PortInfo> inputs, outputs;
    for (const auto& p : node.inputs) inputs.push_back({p.name, p.type});
    for (const auto& p : node.outputs) outputs.push_back({p.name, p.type});

    if (node_content_.type == NodeContentType::VerticalToggle) {
        auto main_row = std::make_unique<Row>();

        auto left_col = std::make_unique<Column>();
        port_layout_builder::build_input_column(*left_col, inputs, port_slots_);

        auto center_col = std::make_unique<Column>();
        auto vt = std::make_unique<VerticalToggleWidget>(node_content_.state, node_content_.tripped);
        auto toggle_container = std::make_unique<Container>(std::move(vt), Edges{0, 5.0f, 0, 5.0f});
        toggle_container->setFlexible(true);
        content_widget_ = center_col->addChild(std::move(toggle_container));
        center_col->setFlexible(true);

        auto right_col = std::make_unique<Column>();
        port_layout_builder::build_output_column(*right_col, outputs, port_slots_);

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
        size_t max_ports = std::max(inputs.size(), outputs.size());
        for (size_t i = 0; i < max_ports; i++) {
            const port_layout_builder::PortInfo* left = (i < inputs.size()) ? &inputs[i] : nullptr;
            const port_layout_builder::PortInfo* right = (i < outputs.size()) ? &outputs[i] : nullptr;
            port_layout_builder::build_port_row(layout_, left, right, port_slots_);
        }

        if (node_content_.type != NodeContentType::None) {
            if (node_content_.type == NodeContentType::Gauge) {
                layout_.addChild(std::make_unique<VoltmeterWidget>(
                    node_content_.value, node_content_.min, node_content_.max, node_content_.unit));
            } else if (node_content_.type == NodeContentType::Switch) {
                float margin = editor_constants::PORT_RADIUS + editor_constants::PORT_LABEL_GAP;
                float v_pad = 2.0f;
                auto sw = std::make_unique<SwitchWidget>(node_content_.state, node_content_.tripped);
                auto content_container = std::make_unique<Container>(std::move(sw), Edges{margin, v_pad, margin, v_pad});
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
                auto content_container = std::make_unique<Container>(std::move(content_inner), Edges{margin, 0, margin, 0});
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
            layout_.child(i)->updateFromContent(node_content_);
        }
    } else if (content_widget_) {
        if (auto* c = dynamic_cast<Container*>(content_widget_)) {
            if (c->child()) {
                c->child()->updateFromContent(node_content_);
            }
        }
    }
}

void VisualNode::render(IDrawList* dl, const Viewport& vp, Pt canvas_min,
                        bool is_selected) const {
    auto bounds = node_frame::world_to_screen(position_, size_, vp, canvas_min);
    float rounding = editor_constants::NODE_ROUNDING * vp.zoom;

    uint32_t fill = node_frame::get_fill_color(custom_color_, render_theme::COLOR_BODY_FILL);
    node_frame::render_fill(*dl, bounds, rounding, fill);

    layout_.render(dl, bounds.min, vp.zoom);

    node_frame::render_border(*dl, bounds, rounding, is_selected);
    node_frame::render_ports(*dl, vp, canvas_min, ports_);
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

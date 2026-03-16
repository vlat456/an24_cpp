#pragma once
#include "visual/widget.h"
#include "visual/port/visual_port.h"
#include "visual/primitives/primitives.h"
#include "visual/container/container.h"
#include "visual/container/linear_layout.h"
#include "visual/node/layout_context.h"
#include "data/port.h"
#include <cmath>
#include <vector>

namespace visual {

/// A PortRow is a single layout unit that pairs a port (edge-anchored)
/// with its label (flex-aligned in the content area).
///
/// During layout(), the PortRow:
/// 1. Lays out the label normally within margins (like a Container+Row)
/// 2. Positions the port so its circle center sits at the node edge
///    (using LayoutContext to know the node width)
///
/// This eliminates the need for a post-layout fixup pass.
/// Ports remain Widget children, preserving hit-testing via worldPos().
class PortRow : public Widget {
public:
    /// Construct a port row for a Left/Right side port.
    /// If port_name is empty, no port is created on that side.
    PortRow(std::string_view port_name, PortSide logical_side, PortType type,
            PortLayoutSide layout_side, const LayoutContext* ctx)
        : layout_side_(layout_side)
        , ctx_(ctx)
    {
        // Create the label
        if (!port_name.empty()) {
            TextAlign align = (layout_side == PortLayoutSide::Right) ? TextAlign::Right : TextAlign::Left;
            label_ = emplaceChild<Label>(port_name, PortConstants::LABEL_FONT_SIZE,
                                         PortConstants::LABEL_COLOR, align);
        }

        // Create the port widget
        if (!port_name.empty()) {
            port_ = emplaceChild<Port>(port_name, logical_side, type, layout_side);
        }
    }

    Port* port() const { return port_; }
    Label* label() const { return label_; }

    Pt preferredSize(IDrawList* dl) const override {
        float label_w = 0;
        if (label_) {
            Pt lps = label_->preferredSize(dl);
            label_w = lps.x;
        }
        // Port indent on the side where the port sits
        float indent = PortConstants::RADIUS * 2 + labelOffset();
        return Pt(label_w + indent, PortConstants::ROW_HEIGHT);
    }

    void layout(float w, float h) override {
        setSize(Pt(w, h));

        // Position label within margins
        float indent = PortConstants::RADIUS * 2 + labelOffset();
        float label_x = 0;
        float label_w = 0;

        if (label_) {
            Pt lps = label_->preferredSize(nullptr);
            label_w = lps.x;
            float label_h = lps.y;

            if (layout_side_ == PortLayoutSide::Left) {
                label_x = indent;
            } else if (layout_side_ == PortLayoutSide::Right) {
                label_x = w - indent - label_w;
            }

            float label_y = (h - label_h) / 2.0f;
            label_->setLocalPos(Pt(label_x, label_y));
            label_->setSize(Pt(label_w, label_h));
        }

        // Position port: circle center at node edge
        if (port_) {
            positionPort(w, h);
        }
    }

    void render(IDrawList* dl, const RenderContext& ctx) const override {}

private:
    PortLayoutSide layout_side_;
    const LayoutContext* ctx_ = nullptr;
    Port* port_ = nullptr;
    Label* label_ = nullptr;

    float labelOffset() const {
        switch (layout_side_) {
            case PortLayoutSide::Left:   return PortConstants::LEFT_LABEL_OFFSET;
            case PortLayoutSide::Right:  return PortConstants::RIGHT_LABEL_OFFSET;
            case PortLayoutSide::Top:    return PortConstants::TOP_LABEL_OFFSET;
            case PortLayoutSide::Bottom: return PortConstants::BOTTOM_LABEL_OFFSET;
        }
        return PortConstants::LEFT_LABEL_OFFSET;
    }

    /// Position port so its circle center sits at the node edge.
    /// Uses the parent chain to compute offset from node origin.
    void positionPort(float row_w, float row_h) {
        if (!port_ || !ctx_) return;

        // Compute the x-offset of this PortRow's origin from the node origin.
        // Walk up the parent chain summing localPos offsets until we reach the root.
        float offset_x = 0;
        float offset_y = 0;
        const Widget* w = this;
        while (w->parent()) {
            offset_x += w->localPos().x;
            offset_y += w->localPos().y;
            w = w->parent();
        }

        float port_lx = 0;
        float port_ly = (row_h - PortConstants::RADIUS * 2) / 2.0f;

        if (layout_side_ == PortLayoutSide::Left) {
            // Port center at node x=0: port_world_x + RADIUS = node_x
            // port_local_x = -offset_x - RADIUS
            port_lx = -offset_x - PortConstants::RADIUS;
        } else if (layout_side_ == PortLayoutSide::Right) {
            // Port center at node x=node_width: port_world_x + RADIUS = node_x + node_width
            // port_local_x = node_width - offset_x - RADIUS
            port_lx = ctx_->node_width - offset_x - PortConstants::RADIUS;
        } else if (layout_side_ == PortLayoutSide::Top) {
            // Port center at node y=0
            port_ly = -offset_y - PortConstants::RADIUS;
            port_lx = (row_w - PortConstants::RADIUS * 2) / 2.0f;
        } else if (layout_side_ == PortLayoutSide::Bottom) {
            // Port center at node y=node_height
            port_ly = ctx_->node_height - offset_y - PortConstants::RADIUS;
            port_lx = (row_w - PortConstants::RADIUS * 2) / 2.0f;
        }

        port_->setLocalPos(Pt(port_lx, port_ly));
    }
};

/// A paired PortRow that handles the standard node layout case:
/// left port + left label on the left, right port + right label on the right,
/// with a flexible gap in between.
class PairedPortRow : public Widget {
public:
    PairedPortRow(std::string_view left_name, PortType left_type,
                  std::string_view right_name, PortType right_type,
                  const LayoutContext* ctx)
        : ctx_(ctx)
    {
        // Left label
        if (!left_name.empty()) {
            left_label_ = emplaceChild<Label>(left_name, PortConstants::LABEL_FONT_SIZE,
                                              PortConstants::LABEL_COLOR, TextAlign::Left);
        }

        // Flexible gap between labels
        gap_ = emplaceChild<Spacer>();

        // Right label
        if (!right_name.empty()) {
            right_label_ = emplaceChild<Label>(right_name, PortConstants::LABEL_FONT_SIZE,
                                               PortConstants::LABEL_COLOR, TextAlign::Right);
        }

        // Ports (created after labels so they render on top)
        if (!left_name.empty()) {
            left_port_ = emplaceChild<Port>(left_name, PortSide::Input, left_type, PortLayoutSide::Left);
        }
        if (!right_name.empty()) {
            right_port_ = emplaceChild<Port>(right_name, PortSide::Output, right_type, PortLayoutSide::Right);
        }
    }

    Port* leftPort() const { return left_port_; }
    Port* rightPort() const { return right_port_; }

    Pt preferredSize(IDrawList* dl) const override {
        float left_indent = PortConstants::RADIUS * 2 + PortConstants::LEFT_LABEL_OFFSET;
        float right_indent = PortConstants::RADIUS * 2 + PortConstants::RIGHT_LABEL_OFFSET;

        float left_w = left_label_ ? left_label_->preferredSize(dl).x : 0;
        float right_w = right_label_ ? right_label_->preferredSize(dl).x : 0;

        float gap = (left_label_ && right_label_) ? PortConstants::MIN_GAP : 0;
        float total_w = left_indent + left_w + gap + right_w + right_indent;

        return Pt(total_w, PortConstants::ROW_HEIGHT);
    }

    void layout(float w, float h) override {
        setSize(Pt(w, h));

        float left_indent = PortConstants::RADIUS * 2 + PortConstants::LEFT_LABEL_OFFSET;
        float right_indent = PortConstants::RADIUS * 2 + PortConstants::RIGHT_LABEL_OFFSET;

        // Position labels
        float v_pad = (h - PortConstants::LABEL_FONT_SIZE) / 2.0f;
        if (v_pad < 0) v_pad = 0;

        if (left_label_) {
            Pt lps = left_label_->preferredSize(nullptr);
            left_label_->setLocalPos(Pt(left_indent, v_pad));
            left_label_->setSize(Pt(lps.x, lps.y));
        }

        if (right_label_) {
            Pt rps = right_label_->preferredSize(nullptr);
            float rx = w - right_indent - rps.x;
            right_label_->setLocalPos(Pt(rx, v_pad));
            right_label_->setSize(Pt(rps.x, rps.y));
        }

        // Position ports at node edges
        positionPorts(w, h);
    }

    void render(IDrawList* dl, const RenderContext& ctx) const override {}

private:
    const LayoutContext* ctx_ = nullptr;
    Label* left_label_ = nullptr;
    Label* right_label_ = nullptr;
    Spacer* gap_ = nullptr;
    Port* left_port_ = nullptr;
    Port* right_port_ = nullptr;

    void positionPorts(float row_w, float row_h) {
        if (!ctx_) return;

        // Compute offset of this widget's origin from the node origin
        float offset_x = 0;
        float offset_y = 0;
        const Widget* w = this;
        while (w->parent()) {
            offset_x += w->localPos().x;
            offset_y += w->localPos().y;
            w = w->parent();
        }

        float port_ly = (row_h - PortConstants::RADIUS * 2) / 2.0f;

        if (left_port_) {
            // Port center at node x=0
            float port_lx = -offset_x - PortConstants::RADIUS;
            left_port_->setLocalPos(Pt(port_lx, port_ly));
        }

        if (right_port_) {
            // Port center at node x=node_width
            float port_lx = ctx_->node_width - offset_x - PortConstants::RADIUS;
            right_port_->setLocalPos(Pt(port_lx, port_ly));
        }
    }
};

/// A horizontal strip of ports distributed evenly along one edge (Top or Bottom).
///
/// During layout(), it:
/// 1. Centers the group of ports across the node width, snapped to the layout grid.
/// 2. Snaps each port's Y to the node edge (top=0 or bottom=node_height).
/// 3. Positions each label centered below (Top) or above (Bottom) its port.
///
/// This eliminates the positionHorizontalPorts() post-layout fixup.
class HorizontalPortStrip : public Widget {
public:
    HorizontalPortStrip(PortLayoutSide side, const LayoutContext* ctx)
        : side_(side)
        , ctx_(ctx)
    {}

    /// Add a port+label pair. Returns the Port* so the caller can track it.
    Port* addPort(std::string_view port_name, PortSide logical_side, PortType type) {
        auto* port_w = emplaceChild<Port>(port_name, logical_side, type, side_);
        auto* label_w = emplaceChild<Label>(port_name, PortConstants::LABEL_FONT_SIZE,
                                             PortConstants::LABEL_COLOR);
        entries_.push_back({port_w, label_w});
        return port_w;
    }

    const std::vector<Port*> ports() const {
        std::vector<Port*> result;
        result.reserve(entries_.size());
        for (const auto& e : entries_) result.push_back(e.port);
        return result;
    }

    Pt preferredSize(IDrawList* dl) const override {
        // Height: port diameter + vertical padding for labels
        constexpr float v_pad = (PortConstants::ROW_HEIGHT - PortConstants::RADIUS * 2) / 2.0f;
        float h = PortConstants::ROW_HEIGHT;

        // Width: we need enough space for the ports spread on a grid.
        // For n ports, need at least (n + 1) * grid width (handled by NodeWidget::preferredSize).
        // Locally, contribute a minimal preferred width.
        float w = 0;
        if (!entries_.empty()) {
            w = static_cast<float>(entries_.size() + 1) * PortConstants::LAYOUT_GRID;
        }
        return Pt(w, h);
    }

    void layout(float w, float h) override {
        setSize(Pt(w, h));
        if (entries_.empty() || !ctx_) return;

        size_t n = entries_.size();
        constexpr float grid = PortConstants::LAYOUT_GRID;
        float node_w = ctx_->node_width;
        float center = node_w / 2.0f;

        // Compute offset of this strip's origin from the node origin.
        float offset_x = 0;
        float offset_y = 0;
        const Widget* walker = this;
        while (walker->parent()) {
            offset_x += walker->localPos().x;
            offset_y += walker->localPos().y;
            walker = walker->parent();
        }

        for (size_t i = 0; i < n; ++i) {
            auto* p = entries_[i].port;
            auto* lbl = entries_[i].label;

            // Calculate ideal centered position, then snap to nearest grid crossing.
            float ideal_x = center + (static_cast<float>(i) - static_cast<float>(n - 1) / 2.0f) * grid;
            float snapped_x = std::round(ideal_x / grid) * grid;

            // Convert to local position relative to this strip
            float lp_x = snapped_x - offset_x - PortConstants::RADIUS;

            // Vertical: snap port center to the node edge
            float lp_y;
            if (side_ == PortLayoutSide::Top) {
                lp_y = -offset_y - PortConstants::RADIUS;
            } else {
                float node_h = ctx_->node_height;
                lp_y = node_h - offset_y - PortConstants::RADIUS;
            }

            p->setLocalPos(Pt(lp_x, lp_y));

            // Position label centered below (top ports) or above (bottom ports)
            if (lbl) {
                Pt label_ps = lbl->preferredSize(nullptr);
                float label_w = label_ps.x;
                float label_x = lp_x + PortConstants::RADIUS - label_w / 2.0f;
                float label_y;
                float label_offset = (side_ == PortLayoutSide::Top) ?
                    PortConstants::TOP_LABEL_OFFSET : PortConstants::BOTTOM_LABEL_OFFSET;
                if (side_ == PortLayoutSide::Top) {
                    label_y = lp_y + PortConstants::RADIUS * 2 + label_offset;
                } else {
                    label_y = lp_y - PortConstants::LABEL_FONT_SIZE - label_offset;
                }
                lbl->setLocalPos(Pt(label_x, label_y));
                lbl->setSize(Pt(label_w, label_ps.y));
            }
        }
    }

    void render(IDrawList* dl, const RenderContext& ctx) const override {}

private:
    struct Entry {
        Port* port = nullptr;
        Label* label = nullptr;
    };

    PortLayoutSide side_;
    const LayoutContext* ctx_ = nullptr;
    std::vector<Entry> entries_;
};

} // namespace visual

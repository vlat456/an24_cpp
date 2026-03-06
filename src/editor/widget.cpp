#include "widget.h"
#include "render.h"
#include <algorithm>
#include <cstring>
#include <cmath>

// ============================================================================
// Widget base
// ============================================================================

Pt Widget::getPreferredSize(IDrawList*) const {
    return Pt(width_, height_);
}

void Widget::layout(float available_width, float available_height) {
    width_ = available_width;
    height_ = available_height;
}

// ============================================================================
// ColumnLayout
// ============================================================================

Widget* ColumnLayout::addWidget(std::unique_ptr<Widget> child) {
    auto* ptr = child.get();
    children_.push_back(std::move(child));
    return ptr;
}

Widget* ColumnLayout::child(size_t i) const {
    return i < children_.size() ? children_[i].get() : nullptr;
}

Pt ColumnLayout::getPreferredSize(IDrawList* dl) const {
    float total_h = 0;
    float max_w = 0;
    for (const auto& c : children_) {
        Pt ps = c->getPreferredSize(dl);
        total_h += ps.y;
        max_w = std::max(max_w, ps.x);
    }
    return Pt(max_w, total_h);
}

void ColumnLayout::layout(float available_width, float available_height) {
    width_ = available_width;
    height_ = available_height;

    // Phase 1: sum fixed heights, count flexible children
    float fixed_total = 0;
    int flex_count = 0;
    for (const auto& c : children_) {
        if (c->isFlexible()) {
            flex_count++;
        } else {
            fixed_total += c->getPreferredSize(nullptr).y;
        }
    }

    // Phase 2: distribute remaining space to flexible children
    float remaining = std::max(0.0f, available_height - fixed_total);
    float flex_height = flex_count > 0 ? remaining / flex_count : 0;

    // Phase 3: position children top-to-bottom
    float y = 0;
    for (auto& c : children_) {
        float child_h;
        if (c->isFlexible()) {
            child_h = flex_height;
        } else {
            child_h = c->getPreferredSize(nullptr).y;
        }
        c->setPosition(0, y);
        c->layout(available_width, child_h);
        y += child_h;
    }
}

void ColumnLayout::render(IDrawList* dl, Pt origin, float zoom) const {
    for (const auto& c : children_) {
        Pt child_origin(origin.x + c->x() * zoom, origin.y + c->y() * zoom);
        c->render(dl, child_origin, zoom);
    }
}

// ============================================================================
// HeaderWidget
// ============================================================================

HeaderWidget::HeaderWidget(const std::string& name, uint32_t fill_color)
    : name_(name), fill_color_(fill_color)
{
    height_ = HEIGHT;
}

Pt HeaderWidget::getPreferredSize(IDrawList*) const {
    return Pt(0, HEIGHT);
}

void HeaderWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = width_ * zoom;
    float vh = VISUAL_HEIGHT * zoom;

    // Header fill (only visual portion)
    dl->add_rect_filled(origin, Pt(origin.x + w, origin.y + vh), fill_color_);

    // Name text centered vertically in visual header
    float font = FONT_SIZE * zoom;
    Pt text_pos(origin.x + PADDING * zoom, origin.y + vh / 2 - font / 2);
    dl->add_text(text_pos, name_.c_str(), 0xFFFFFFFF, font);
}

// ============================================================================
// TypeNameWidget
// ============================================================================

TypeNameWidget::TypeNameWidget(const std::string& type_name)
    : type_name_(type_name)
{
    height_ = HEIGHT;
}

Pt TypeNameWidget::getPreferredSize(IDrawList*) const {
    return Pt(0, HEIGHT);
}

void TypeNameWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = width_ * zoom;
    float font = FONT_SIZE * zoom;

    Pt text_size = dl->calc_text_size(type_name_.c_str(), font);
    // Centered horizontally, vertically centered in row
    float tx = origin.x + (w - text_size.x) / 2;
    float ty = origin.y + (height_ * zoom - font) / 2;
    dl->add_text(Pt(tx, ty), type_name_.c_str(), 0xFFAAAAAA, font);
}

// ============================================================================
// PortRowWidget
// ============================================================================

PortRowWidget::PortRowWidget(const std::string& left_port, const std::string& right_port,
                             an24::PortType left_type, an24::PortType right_type)
    : left_port_(left_port), right_port_(right_port)
    , left_type_(left_type), right_type_(right_type)
{
    height_ = ROW_HEIGHT;
}

float PortRowWidget::estimateLabelWidth(const std::string& label) {
    if (label.empty()) return 0;
    return label.length() * LABEL_FONT_SIZE * 0.6f;
}

Pt PortRowWidget::getPreferredSize(IDrawList*) const {
    return Pt(0, ROW_HEIGHT);
}

void PortRowWidget::layout(float available_width, float available_height) {
    width_ = available_width;
    height_ = available_height;

    left_label_width_ = estimateLabelWidth(left_port_);
    right_label_width_ = estimateLabelWidth(right_port_);

    // Content area: between left label end and right label start
    float left_edge = PORT_RADIUS + LABEL_GAP + left_label_width_;
    float right_edge = available_width - PORT_RADIUS - LABEL_GAP - right_label_width_;
    if (right_edge < left_edge) right_edge = left_edge;

    content_bounds_ = {left_edge, 0, right_edge - left_edge, available_height};
}

Pt PortRowWidget::leftPortCenter() const {
    return Pt(0, y_ + height_ / 2);
}

Pt PortRowWidget::rightPortCenter() const {
    return Pt(width_, y_ + height_ / 2);
}

void PortRowWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = width_ * zoom;
    float h = height_ * zoom;
    float center_y = origin.y + h / 2;
    float r = PORT_RADIUS * zoom;
    float gap = LABEL_GAP * zoom;
    float font = LABEL_FONT_SIZE * zoom;

    // Left port: circle centered at left edge of node
    if (!left_port_.empty()) {
        Pt left_center(origin.x, center_y);
        uint32_t left_color = get_port_color(left_type_);
        dl->add_circle_filled(left_center, r, left_color, 8);

        // Label right of circle
        Pt label_pos(origin.x + r + gap, center_y - font / 2);
        dl->add_text(label_pos, left_port_.c_str(), COLOR_TEXT_DIM, font);
    }

    // Right port: circle centered at right edge of node
    if (!right_port_.empty()) {
        Pt right_center(origin.x + w, center_y);
        uint32_t right_color = get_port_color(right_type_);
        dl->add_circle_filled(right_center, r, right_color, 8);

        // Label left of circle (right-aligned text)
        Pt text_size = dl->calc_text_size(right_port_.c_str(), font);
        Pt label_pos(origin.x + w - r - gap - text_size.x, center_y - font / 2);
        dl->add_text(label_pos, right_port_.c_str(), COLOR_TEXT_DIM, font);
    }
}

// ============================================================================
// ContentWidget
// ============================================================================

ContentWidget::ContentWidget(const std::string& label, float value,
                             float left_margin, float right_margin)
    : label_(label), value_(value),
      left_margin_(left_margin), right_margin_(right_margin)
{
    flexible_ = true;
}

Pt ContentWidget::getPreferredSize(IDrawList*) const {
    return Pt(0, 16.0f); // minimum preferred height
}

void ContentWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    if (label_.empty()) return;

    float w = width_ * zoom;
    float h = height_ * zoom;
    float lm = left_margin_ * zoom;
    float rm = right_margin_ * zoom;
    float font = FONT_SIZE * zoom;

    // Render label centered in available content area
    float content_x = origin.x + lm;
    float content_w = w - lm - rm;
    if (content_w <= 0) return;

    Pt text_size = dl->calc_text_size(label_.c_str(), font);
    float tx = content_x + (content_w - text_size.x) / 2;
    float ty = origin.y + (h - font) / 2;
    dl->add_text(Pt(tx, ty), label_.c_str(), COLOR_TEXT_DIM, font);
}

// ============================================================================
// VoltmeterWidget - Visual steam gauge
// ============================================================================

VoltmeterWidget::VoltmeterWidget(float value, float min_val, float max_val,
                                 const std::string& unit)
    : value_(value), min_val_(min_val), max_val_(max_val), unit_(unit)
{
    flexible_ = false;
    height_ = GAUGE_RADIUS * 2.0f + VALUE_FONT_SIZE + UNIT_FONT_SIZE + 10.0f;
}

Pt VoltmeterWidget::getPreferredSize(IDrawList*) const {
    return Pt(GAUGE_RADIUS * 2.0f, height_);
}

void VoltmeterWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float cx = origin.x + (width_ * zoom) / 2.0f;
    float cy = origin.y + GAUGE_RADIUS * zoom;
    float r = GAUGE_RADIUS * zoom;
    float needle_len = NEEDLE_LENGTH * zoom;

    // Helper: convert gauge angle (degrees, math convention) to screen point
    // Uses cy - sin to flip Y for screen coords (Y-down)
    constexpr float DEG2RAD = 3.14159265f / 180.0f;
    auto angle_to_pt = [&](float angle_deg, float radius) -> Pt {
        float rad = angle_deg * DEG2RAD;
        return Pt(cx + std::cos(rad) * radius, cy - std::sin(rad) * radius);
    };

    // Draw gauge background arc using polyline
    constexpr int arc_segments = 32;
    Pt arc_points[arc_segments + 1];
    for (int i = 0; i <= arc_segments; ++i) {
        float t = static_cast<float>(i) / arc_segments;
        float angle_deg = START_ANGLE + t * SWEEP_ANGLE;
        arc_points[i] = angle_to_pt(angle_deg, r);
    }
    dl->add_polyline(arc_points, arc_segments + 1, COLOR_GAUGE_BORDER, 2.0f * zoom);

    // Draw tick marks
    int num_ticks = 11;  // 0, 10%, 20%, ..., 100%
    for (int i = 0; i < num_ticks; ++i) {
        float t = static_cast<float>(i) / (num_ticks - 1);
        float angle_deg = START_ANGLE + t * SWEEP_ANGLE;

        bool is_major = (i % 5 == 0);
        float tick_len = is_major ? 6.0f : 3.0f;
        uint32_t tick_color = is_major ? COLOR_TICK_MAJOR : COLOR_TICK_MINOR;

        Pt tick_outer = angle_to_pt(angle_deg, r);
        Pt tick_inner = angle_to_pt(angle_deg, r - tick_len * zoom);

        dl->add_line(tick_outer, tick_inner, tick_color, 1.5f * zoom);
    }

    // Calculate needle angle from value
    float clamped_val = std::clamp(value_, min_val_, max_val_);
    float normalized = (clamped_val - min_val_) / (max_val_ - min_val_);
    float needle_angle_deg = START_ANGLE + normalized * SWEEP_ANGLE;

    // Draw needle
    Pt needle_tip = angle_to_pt(needle_angle_deg, needle_len);
    dl->add_line(Pt(cx, cy), needle_tip, COLOR_NEEDLE, 2.0f * zoom);

    // Draw center pivot
    dl->add_circle_filled(Pt(cx, cy), 3.0f * zoom, COLOR_NEEDLE);

    // Draw value text below gauge
    char value_buf[32];
    snprintf(value_buf, sizeof(value_buf), "%.1f", value_);
    float value_font = VALUE_FONT_SIZE * zoom;
    float unit_font = UNIT_FONT_SIZE * zoom;

    Pt value_size = dl->calc_text_size(value_buf, value_font);
    Pt value_pos(cx - value_size.x / 2.0f,
                 origin.y + (GAUGE_RADIUS * 2.0f + 5.0f) * zoom);
    dl->add_text(value_pos, value_buf, COLOR_TEXT, value_font);

    // Draw unit text
    Pt unit_size = dl->calc_text_size(unit_.c_str(), unit_font);
    Pt unit_pos(cx - unit_size.x / 2.0f,
                value_pos.y + value_font + 2.0f * zoom);
    dl->add_text(unit_pos, unit_.c_str(), 0xFFAAAAAA, unit_font);
}

#include "visual/node/widget.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
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
// HeaderWidget
// ============================================================================

HeaderWidget::HeaderWidget(const std::string& name, uint32_t fill_color, float rounding)
    : name_(name), fill_color_(fill_color), rounding_(rounding)
{
    height_ = HEIGHT;
}

Pt HeaderWidget::getPreferredSize(IDrawList* dl) const {
    // Calculate width based on name text
    float name_w = 0;
    if (!name_.empty()) {
        if (dl) {
            Pt text_size = dl->calc_text_size(name_.c_str(), FONT_SIZE);
            name_w = text_size.x;
        } else {
            // Estimate width when dl not available
            name_w = estimateTextWidth(name_);
        }
    }

    // Total width = padding + name + padding
    float total_w = PADDING + name_w + PADDING;

    return Pt(total_w, HEIGHT);
}

void HeaderWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = width_ * zoom;
    float vh = VISUAL_HEIGHT * zoom;

    // Header fill — use rounded top corners when rounding is set
    float r = rounding_ * zoom;
    if (r > 0.0f) {
        // 0x30 = ImDrawFlags_RoundCornersTop (TopLeft|TopRight), value is stable in ImGui
        dl->add_rect_filled_with_rounding_corners(
            origin, Pt(origin.x + w, origin.y + vh), fill_color_, r, 0x30);
    } else {
        dl->add_rect_filled(origin, Pt(origin.x + w, origin.y + vh), fill_color_);
    }

    // Name text centered vertically in visual header
    float font = FONT_SIZE * zoom;
    Pt text_pos(origin.x + PADDING * zoom, origin.y + vh / 2 - font / 2);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, font);
}

// ============================================================================
// TypeNameWidget
// ============================================================================

TypeNameWidget::TypeNameWidget(const std::string& type_name)
    : type_name_(type_name)
{
    height_ = HEIGHT;
}

Pt TypeNameWidget::getPreferredSize(IDrawList* dl) const {
    // Calculate width based on type name
    float name_w = 0;
    if (!type_name_.empty()) {
        if (dl) {
            Pt text_size = dl->calc_text_size(type_name_.c_str(), FONT_SIZE);
            name_w = text_size.x;
        } else {
            // Estimate width when dl not available
            name_w = type_name_.length() * FONT_SIZE * 0.6f;
        }
    }

    // Total width = text width (centered, so no extra padding)
    return Pt(name_w, HEIGHT);
}

void TypeNameWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = width_ * zoom;
    float font = FONT_SIZE * zoom;

    Pt text_size = dl->calc_text_size(type_name_.c_str(), font);
    // Centered horizontally, vertically centered in row
    float tx = origin.x + (w - text_size.x) / 2;
    float ty = origin.y + (height_ * zoom - font) / 2;
    dl->add_text(Pt(tx, ty), type_name_.c_str(), render_theme::COLOR_TEXT_DIM, font);
}

// ============================================================================
// SwitchWidget - Rect-based toggle button
// ============================================================================

SwitchWidget::SwitchWidget(bool state, bool tripped)
    : state_(state), tripped_(tripped)
{
    flexible_ = false;
    height_ = HEIGHT;
}

Pt SwitchWidget::getPreferredSize(IDrawList*) const {
    return Pt(MIN_WIDTH, HEIGHT);
}

void SwitchWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = width_ * zoom;
    float h = HEIGHT * zoom;
    float r = ROUNDING * zoom;

    Pt min = origin;
    Pt max(origin.x + w, origin.y + h);

    // Pick fill color based on state
    uint32_t fill;
    if (tripped_) {
        fill = render_theme::COLOR_TRIPPED;
    } else if (state_) {
        fill = 0xFF3A6830;  // Muted green (ON)
    } else {
        fill = 0xFF1C1D24;  // Surface 0 (OFF)
    }

    dl->add_rect_filled_with_rounding(min, max, fill, r);
    dl->add_rect_with_rounding_corners(min, max, render_theme::COLOR_BUS_BORDER, r,
                                        0xF0, 1.0f);  // 0xF0 = all corners

    // Label text
    const char* label = tripped_ ? "TRIP" : (state_ ? "ON" : "OFF");
    float font = FONT_SIZE * zoom;
    Pt text_size = dl->calc_text_size(label, font);
    float tx = origin.x + (w - text_size.x) / 2.0f;
    float ty = origin.y + (h - font) / 2.0f;
    uint32_t text_color = tripped_ ? 0xFFFFFFFF : render_theme::COLOR_TEXT;
    dl->add_text(Pt(tx, ty), label, text_color, font);
}

// ============================================================================
// VerticalToggleWidget - Vertical slider toggle (up=ON, down=OFF)
// ============================================================================

VerticalToggleWidget::VerticalToggleWidget(bool state, bool tripped)
    : state_(state), tripped_(tripped)
{
    flexible_ = false;
    width_ = WIDTH;
    height_ = HEIGHT;
}

Pt VerticalToggleWidget::getPreferredSize(IDrawList*) const {
    return Pt(WIDTH, HEIGHT);
}

void VerticalToggleWidget::render(IDrawList* dl, Pt origin, float zoom) const {
    float w = WIDTH * zoom;
    float h = HEIGHT * zoom;
    float track_w = TRACK_WIDTH * zoom;
    float handle_sz = HANDLE_SIZE * zoom;
    float r = ROUNDING * zoom;

    float cx = origin.x + w / 2.0f;  // Center X of the widget

    // Track background (vertical groove)
    float track_x = cx - track_w / 2.0f;
    Pt track_min(track_x, origin.y);
    Pt track_max(track_x + track_w, origin.y + h);
    dl->add_rect_filled_with_rounding(track_min, track_max, 0xFF1C1D24, r);

    // Handle position: up=ON, down=OFF
    // ON position = near top (small Y offset from origin.y)
    // OFF position = near bottom (near origin.y + h)
    float handle_y = state_
        ? origin.y + h * 0.15f  // ON: 15% from top
        : origin.y + h * 0.70f; // OFF: 70% from top (leaves room at bottom)

    Pt handle_min(cx - handle_sz / 2.0f, handle_y);
    Pt handle_max(cx + handle_sz / 2.0f, handle_y + handle_sz);

    // Handle fill color based on state
    uint32_t handle_fill;
    uint32_t handle_border;
    if (tripped_) {
        handle_fill = render_theme::COLOR_TRIPPED;
        handle_border = 0xFF204080;  // Darker red border
    } else if (state_) {
        handle_fill = 0xFF3A6830;    // Muted green (ON)
        handle_border = 0xFF284820;  // Darker green border
    } else {
        handle_fill = 0xFF2C3038;    // Neutral gray (OFF)
        handle_border = 0xFF1C1D24;  // Darker gray border
    }

    // Draw handle with rounded corners
    dl->add_rect_filled_with_rounding(handle_min, handle_max, handle_fill, r);
    dl->add_rect_with_rounding_corners(handle_min, handle_max, handle_border, r,
                                        0xF0, 1.0f * zoom);

    // Optional: small indicator line on handle to show "grip" texture
    uint32_t grip_color = tripped_ ? 0xFFFFFFFF : render_theme::COLOR_TEXT_DIM;
    float grip_y = handle_y + handle_sz / 2.0f;
    float grip_w = 2.0f * zoom;
    float grip_h = 6.0f * zoom;
    dl->add_rect_filled(Pt(cx - grip_w / 2.0f, grip_y - grip_h / 2.0f),
                        Pt(cx + grip_w / 2.0f, grip_y + grip_h / 2.0f),
                        grip_color);
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
    dl->add_text(unit_pos, unit_.c_str(), render_theme::COLOR_TEXT_DIM, unit_font);
}

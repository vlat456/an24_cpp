#include "content_widgets.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
#include "visual/render_context.h"
#include "data/node_content.h"
#include <algorithm>
#include <cmath>

namespace visual {

HeaderWidget::HeaderWidget(const std::string& name, uint32_t fill_color, float rounding)
    : name_(name), fill_color_(fill_color), rounding_(rounding)
{
    setSize(Pt(0, HEIGHT));
}

Pt HeaderWidget::preferredSize(IDrawList* dl) const {
    float name_w = 0;
    if (!name_.empty()) {
        if (dl) {
            Pt text_size = dl->calc_text_size(name_.c_str(), FONT_SIZE);
            name_w = text_size.x;
        } else {
            name_w = estimateTextWidth(name_);
        }
    }
    float total_w = PADDING + name_w + PADDING;
    return Pt(total_w, HEIGHT);
}

void HeaderWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    Pt origin = ctx.world_to_screen(worldPos());
    float zoom = ctx.zoom;
    float w = size().x * zoom;
    float vh = VISUAL_HEIGHT * zoom;
    float r = rounding_ * zoom;

    if (r > 0.0f) {
        dl->add_rect_filled_with_rounding_corners(
            origin, Pt(origin.x + w, origin.y + vh), fill_color_, r, 0x30);
    } else {
        dl->add_rect_filled(origin, Pt(origin.x + w, origin.y + vh), fill_color_);
    }

    float font = FONT_SIZE * zoom;
    Pt text_pos(origin.x + PADDING * zoom, origin.y + (VISUAL_HEIGHT - FONT_SIZE) * zoom / 2.0f);
    dl->add_text(text_pos, name_.c_str(), render_theme::COLOR_TEXT, font);
}

TypeNameWidget::TypeNameWidget(const std::string& type_name)
    : type_name_(type_name)
{
    setSize(Pt(0, HEIGHT));
}

Pt TypeNameWidget::preferredSize(IDrawList* dl) const {
    float name_w = 0;
    if (!type_name_.empty()) {
        if (dl) {
            Pt text_size = dl->calc_text_size(type_name_.c_str(), FONT_SIZE);
            name_w = text_size.x;
        } else {
            name_w = type_name_.length() * FONT_SIZE * 0.6f;
        }
    }
    return Pt(name_w, HEIGHT);
}

void TypeNameWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    Pt origin = ctx.world_to_screen(worldPos());
    float zoom = ctx.zoom;
    float w = size().x * zoom;
    float font = FONT_SIZE * zoom;

    Pt text_size = dl->calc_text_size(type_name_.c_str(), font);
    float tx = origin.x + w - text_size.x - RIGHT_PADDING * zoom;
    float ty = origin.y + (HEIGHT * zoom - font) / 2;
    dl->add_text(Pt(tx, ty), type_name_.c_str(), render_theme::COLOR_TEXT_DIM, font);
}

SwitchWidget::SwitchWidget(bool state, bool tripped)
    : state_(state), tripped_(tripped)
{
    setFlexible(false);
    setSize(Pt(MIN_WIDTH, HEIGHT));
}

Pt SwitchWidget::preferredSize(IDrawList*) const {
    return Pt(MIN_WIDTH, HEIGHT);
}

void SwitchWidget::layout(float w, float h) {
    // Accept the full space from the parent but keep our natural height.
    // render() will center the actual toggle within this box.
    setSize(Pt(w, HEIGHT));
}

void SwitchWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    Pt origin = ctx.world_to_screen(worldPos());
    float zoom = ctx.zoom;
    float box_w = size().x * zoom;
    float w = MIN_WIDTH * zoom;  // Always draw at natural width
    float h = HEIGHT * zoom;
    float r = ROUNDING * zoom;

    // Center the toggle horizontally within the allocated box
    float offset_x = (box_w - w) / 2.0f;
    Pt min(origin.x + offset_x, origin.y);
    Pt max(min.x + w, origin.y + h);

    uint32_t fill;
    if (tripped_) {
        fill = render_theme::COLOR_TRIPPED;
    } else if (state_) {
        fill = 0xFF3A6830;
    } else {
        fill = 0xFF1C1D24;
    }

    dl->add_rect_filled_with_rounding(min, max, fill, r);
    dl->add_rect_with_rounding_corners(min, max, render_theme::COLOR_BUS_BORDER, r, 0xF0, 1.0f);

    const char* label = tripped_ ? "TRIP" : (state_ ? "ON" : "OFF");
    float font = FONT_SIZE * zoom;
    Pt text_size = dl->calc_text_size(label, font);
    float tx = min.x + (w - text_size.x) / 2.0f;
    float ty = origin.y + (h - font) / 2.0f;
    uint32_t text_color = tripped_ ? 0xFFFFFFFF : render_theme::COLOR_TEXT;
    dl->add_text(Pt(tx, ty), label, text_color, font);
}

void SwitchWidget::updateFromContent(const NodeContent& content) {
    state_ = content.state;
    tripped_ = content.tripped;
}

VerticalToggleWidget::VerticalToggleWidget(bool state, bool tripped)
    : state_(state), tripped_(tripped)
{
    setFlexible(false);
    setSize(Pt(WIDTH, HEIGHT));
}

Pt VerticalToggleWidget::preferredSize(IDrawList*) const {
    return Pt(WIDTH, HEIGHT);
}

void VerticalToggleWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    Pt origin = ctx.world_to_screen(worldPos());
    float zoom = ctx.zoom;
    float w = WIDTH * zoom;
    float h = HEIGHT * zoom;
    float track_w = TRACK_WIDTH * zoom;
    float handle_sz = HANDLE_SIZE * zoom;
    float r = ROUNDING * zoom;

    float cx = origin.x + w / 2.0f;

    float track_x = cx - track_w / 2.0f;
    Pt track_min(track_x, origin.y);
    Pt track_max(track_x + track_w, origin.y + h);
    dl->add_rect_filled_with_rounding(track_min, track_max, 0xFF1C1D24, r);

    float handle_y = state_
        ? origin.y + h * 0.15f
        : origin.y + h * 0.70f;

    Pt handle_min(cx - handle_sz / 2.0f, handle_y);
    Pt handle_max(cx + handle_sz / 2.0f, handle_y + handle_sz);

    uint32_t handle_fill;
    uint32_t handle_border;
    if (tripped_) {
        handle_fill = render_theme::COLOR_TRIPPED;
        handle_border = 0xFF204080;
    } else if (state_) {
        handle_fill = 0xFF3A6830;
        handle_border = 0xFF284820;
    } else {
        handle_fill = 0xFF2C3038;
        handle_border = 0xFF1C1D24;
    }

    dl->add_rect_filled_with_rounding(handle_min, handle_max, handle_fill, r);
    dl->add_rect_with_rounding_corners(handle_min, handle_max, handle_border, r, 0xF0, 1.0f * zoom);

    uint32_t grip_color = tripped_ ? 0xFFFFFFFF : render_theme::COLOR_TEXT_DIM;
    float grip_y = handle_y + handle_sz / 2.0f;
    float grip_w = 2.0f * zoom;
    float grip_h = 6.0f * zoom;
    dl->add_rect_filled(Pt(cx - grip_w / 2.0f, grip_y - grip_h / 2.0f),
                        Pt(cx + grip_w / 2.0f, grip_y + grip_h / 2.0f),
                        grip_color);
}

void VerticalToggleWidget::updateFromContent(const NodeContent& content) {
    state_ = content.state;
    tripped_ = content.tripped;
}

// ============================================================================
// SliderWidget
// ============================================================================

SliderWidget::SliderWidget(float value, float min_val, float max_val)
    : value_(value), min_val_(min_val), max_val_(max_val)
{
    setFlexible(false);
    setSize(Pt(MIN_WIDTH, HEIGHT));
}

Pt SliderWidget::preferredSize(IDrawList*) const {
    return Pt(MIN_WIDTH, HEIGHT);
}

void SliderWidget::layout(float w, float h) {
    setSize(Pt(w, HEIGHT));
}

float SliderWidget::normalizedFromLocalX(float local_x) const {
    float pad = HANDLE_RADIUS;
    float track_w = size().x - 2.0f * pad;
    if (track_w <= 0.0f) return 0.0f;
    float t = (local_x - pad) / track_w;
    return std::clamp(t, 0.0f, 1.0f);
}

void SliderWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    Pt origin = ctx.world_to_screen(worldPos());
    float zoom = ctx.zoom;
    float w = size().x * zoom;
    float h = HEIGHT * zoom;
    float pad = HANDLE_RADIUS * zoom;
    float track_h = TRACK_HEIGHT * zoom;
    float r = ROUNDING * zoom;

    // Track background
    float track_y = origin.y + (h - track_h) / 2.0f;
    Pt track_min(origin.x + pad, track_y);
    Pt track_max(origin.x + w - pad, track_y + track_h);
    dl->add_rect_filled_with_rounding(track_min, track_max, 0xFF1C1D24, r);

    // Filled portion
    float range = max_val_ - min_val_;
    float t = (range > 1e-6f) ? std::clamp((value_ - min_val_) / range, 0.0f, 1.0f) : 0.0f;
    float track_w = w - 2.0f * pad;
    float fill_w = t * track_w;
    if (fill_w > 0.5f) {
        Pt fill_max(track_min.x + fill_w, track_y + track_h);
        dl->add_rect_filled_with_rounding(track_min, fill_max, 0xFF3A6830, r);
    }

    // Handle circle
    float cx = track_min.x + t * track_w;
    float cy = origin.y + h / 2.0f;
    float handle_r = HANDLE_RADIUS * zoom;
    dl->add_circle_filled(Pt(cx, cy), handle_r, 0xFF5078C0, 16);
    dl->add_circle(Pt(cx, cy), handle_r, 0xFF3050A0, 16);

    // Value text below track
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", value_);
    float font = FONT_SIZE * zoom;
    Pt text_sz = dl->calc_text_size(buf, font);
    float tx = origin.x + (w - text_sz.x) / 2.0f;
    float ty = track_max.y + 1.0f * zoom;
    dl->add_text(Pt(tx, ty), buf, render_theme::COLOR_TEXT_DIM, font);
}

void SliderWidget::updateFromContent(const NodeContent& content) {
    value_ = content.value;
    min_val_ = content.min;
    max_val_ = content.max;
}

VoltmeterWidget::VoltmeterWidget(float value, float min_val, float max_val,
                                 const std::string& unit)
    : value_(value), min_val_(min_val), max_val_(max_val), unit_(unit)
{
    setFlexible(false);
    float h = GAUGE_RADIUS * 2.0f + VALUE_FONT_SIZE + UNIT_FONT_SIZE + 10.0f;
    setSize(Pt(GAUGE_RADIUS * 2.0f, h));
}

Pt VoltmeterWidget::preferredSize(IDrawList*) const {
    return Pt(GAUGE_RADIUS * 2.0f, size().y);
}

void VoltmeterWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    Pt origin = ctx.world_to_screen(worldPos());
    float zoom = ctx.zoom;
    float cx = origin.x + (size().x * zoom) / 2.0f;
    float cy = origin.y + GAUGE_RADIUS * zoom;
    float r = GAUGE_RADIUS * zoom;
    float needle_len = NEEDLE_LENGTH * zoom;

    constexpr float DEG2RAD = 3.14159265f / 180.0f;
    auto angle_to_pt = [&](float angle_deg, float radius) -> Pt {
        float rad = angle_deg * DEG2RAD;
        return Pt(cx + std::cos(rad) * radius, cy - std::sin(rad) * radius);
    };

    constexpr int arc_segments = 32;
    Pt arc_points[arc_segments + 1];
    for (int i = 0; i <= arc_segments; ++i) {
        float t = static_cast<float>(i) / arc_segments;
        float angle_deg = START_ANGLE + t * SWEEP_ANGLE;
        arc_points[i] = angle_to_pt(angle_deg, r);
    }
    dl->add_polyline(arc_points, arc_segments + 1, COLOR_GAUGE_BORDER, 2.0f * zoom);

    int num_ticks = 11;
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

    float clamped_val = std::clamp(value_, min_val_, max_val_);
    float range = max_val_ - min_val_;
    float normalized = (range > 1e-6f) ? (clamped_val - min_val_) / range : 0.0f;
    float needle_angle_deg = START_ANGLE + normalized * SWEEP_ANGLE;

    Pt needle_tip = angle_to_pt(needle_angle_deg, needle_len);
    dl->add_line(Pt(cx, cy), needle_tip, COLOR_NEEDLE, 2.0f * zoom);

    dl->add_circle_filled(Pt(cx, cy), 3.0f * zoom, COLOR_NEEDLE);

    char value_buf[32];
    snprintf(value_buf, sizeof(value_buf), "%.1f", value_);
    float value_font = VALUE_FONT_SIZE * zoom;
    float unit_font = UNIT_FONT_SIZE * zoom;

    Pt value_size = dl->calc_text_size(value_buf, value_font);
    Pt value_pos(cx - value_size.x / 2.0f,
                 origin.y + (GAUGE_RADIUS * 2.0f + 5.0f) * zoom);
    dl->add_text(value_pos, value_buf, COLOR_TEXT, value_font);

    Pt unit_size = dl->calc_text_size(unit_.c_str(), unit_font);
    Pt unit_pos(cx - unit_size.x / 2.0f,
                value_pos.y + value_font + 2.0f * zoom);
    dl->add_text(unit_pos, unit_.c_str(), render_theme::COLOR_TEXT_DIM, unit_font);
}

void VoltmeterWidget::updateFromContent(const NodeContent& content) {
    value_ = content.value;
    min_val_ = content.min;
    max_val_ = content.max;
}

// ============================================================================
// IndicatorWidget
// ============================================================================

IndicatorWidget::IndicatorWidget(float brightness)
    : brightness_(brightness)
{
    setFlexible(false);
    setSize(Pt(SIZE, SIZE));
}

Pt IndicatorWidget::preferredSize(IDrawList*) const {
    return Pt(SIZE, SIZE);
}

void IndicatorWidget::layout(float w, float h) {
    Widget::layout(w, h);
}

void IndicatorWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    Pt origin = ctx.world_to_screen(worldPos());
    float zoom = ctx.zoom;

    // Circle radius: larger when brighter (0.3 to 0.45 of SIZE)
    float brightness = std::clamp(brightness_, 0.0f, 1.0f);
    float r_base = 0.3f + 0.15f * brightness;
    float r = SIZE * r_base * zoom;

    float cx = origin.x + size().x * zoom * 0.5f;
    float cy = origin.y + size().y * zoom * 0.5f;

    uint32_t fill_color;
    if (brightness <= 0.0f) {
        fill_color = COLOR_OFF;
    } else {
        // Interpolate from gray to green based on brightness
        uint8_t g = static_cast<uint8_t>(48 + 207 * brightness);
        uint8_t r_col = static_cast<uint8_t>(48 * (1.0f - brightness));
        uint8_t b_col = static_cast<uint8_t>(48 * (1.0f - brightness));
        // Alpha: more transparent when dimmer
        uint8_t alpha = static_cast<uint8_t>(80 + 175 * brightness);
        fill_color = (alpha << 24) | (b_col << 16) | (g << 8) | r_col;
    }

    dl->add_circle_filled(Pt(cx, cy), r, fill_color, 16);
    dl->add_circle(Pt(cx, cy), r, 0xFF404040, 16);
}

void IndicatorWidget::updateFromContent(const NodeContent& content) {
    brightness_ = std::clamp(content.value, 0.0f, 1.0f);
}

// ============================================================================
// KnobWidget
// ============================================================================

KnobWidget::KnobWidget(int position, int num_positions)
    : position_(position), num_positions_(num_positions)
{
    setFlexible(false);
    setSize(Pt(SIZE, SIZE));
}

Pt KnobWidget::preferredSize(IDrawList*) const {
    return Pt(SIZE, SIZE);
}

void KnobWidget::layout(float w, float h) {
    Widget::layout(w, h);
}

void KnobWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    Pt origin = ctx.world_to_screen(worldPos());
    float zoom = ctx.zoom;

    float cx = origin.x + size().x * zoom * 0.5f;
    float cy = origin.y + size().y * zoom * 0.5f;

    constexpr float DEG2RAD = 3.14159265f / 180.0f;
    auto angle_to_pt = [&](float angle_deg, float radius) -> Pt {
        float rad = angle_deg * DEG2RAD;
        return Pt(cx + std::cos(rad) * radius, cy - std::sin(rad) * radius);
    };

    // Draw tick marks for each position
    float knob_r = KNOB_RADIUS * zoom;
    float tick_in = TICK_INNER * zoom;
    float tick_out = TICK_OUTER * zoom;

    for (int i = 0; i < num_positions_; ++i) {
        float t = (num_positions_ > 1) 
            ? static_cast<float>(i) / (num_positions_ - 1) 
            : 0.5f;
        float angle = ARC_START_DEG + t * ARC_SWEEP_DEG;

        Pt inner = angle_to_pt(angle, tick_in);
        Pt outer = angle_to_pt(angle, tick_out);

        uint32_t color = (i == position_) ? COLOR_TICK_ACTIVE : COLOR_TICK;
        float thickness = (i == position_) ? 2.5f * zoom : 1.5f * zoom;
        dl->add_line(inner, outer, color, thickness);
    }

    // Draw knob circle
    dl->add_circle_filled(Pt(cx, cy), knob_r, COLOR_KNOB_FILL, 24);
    dl->add_circle(Pt(cx, cy), knob_r, COLOR_KNOB_BORDER, 24);

    // Draw indicator line from center toward selected position
    float sel_t = (num_positions_ > 1) 
        ? static_cast<float>(position_) / (num_positions_ - 1) 
        : 0.5f;
    float sel_angle = ARC_START_DEG + sel_t * ARC_SWEEP_DEG;
    Pt indicator_tip = angle_to_pt(sel_angle, knob_r * 0.85f);
    dl->add_line(Pt(cx, cy), indicator_tip, COLOR_INDICATOR, 2.0f * zoom);

    // Draw small dot at indicator tip
    dl->add_circle_filled(indicator_tip, 2.5f * zoom, COLOR_INDICATOR, 8);

    // Position label below knob
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", position_ + 1);  // 1-based display
    float font = FONT_SIZE * zoom;
    Pt text_sz = dl->calc_text_size(buf, font);
    float tx = cx - text_sz.x / 2.0f;
    float ty = origin.y + size().y * zoom - font;
    dl->add_text(Pt(tx, ty), buf, render_theme::COLOR_TEXT_DIM, font);
}

void KnobWidget::updateFromContent(const NodeContent& content) {
    position_ = static_cast<int>(content.value);
    num_positions_ = static_cast<int>(content.max);
    if (num_positions_ < 2) num_positions_ = 2;
    if (position_ < 0) position_ = 0;
    if (position_ >= num_positions_) position_ = num_positions_ - 1;
}

} // namespace visual

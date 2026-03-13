#include "content_widgets.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
#include "visual/render_context.h"
#include "data/node.h"
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
    float tx = origin.x + (w - text_size.x) / 2;
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

void SwitchWidget::render(IDrawList* dl, const RenderContext& ctx) const {
    Pt origin = ctx.world_to_screen(worldPos());
    float zoom = ctx.zoom;
    float w = size().x * zoom;
    float h = HEIGHT * zoom;
    float r = ROUNDING * zoom;

    Pt min = origin;
    Pt max(origin.x + w, origin.y + h);

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
    float tx = origin.x + (w - text_size.x) / 2.0f;
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
}

} // namespace visual

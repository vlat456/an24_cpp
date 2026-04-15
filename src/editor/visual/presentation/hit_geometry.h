#pragma once

#include "editor/layout_constants.h"
#include "editor/input/input_types.h"
#include "editor/visual/presentation/node_presentation.h"
#include "editor/visual/port/visual_port.h"
#include "ui/math/pt.h"
#include "ui/math/rect.h"

#include <algorithm>
#include <cmath>

namespace editor::presentation::hit_geometry {

inline constexpr float port_hit_radius() {
    return visual::PortConstants::HIT_RADIUS;
}

inline constexpr float routing_point_hit_radius() {
    return editor_constants::ROUTING_POINT_HIT_RADIUS;
}

inline constexpr float routing_point_render_radius() {
    return editor_constants::ROUTING_POINT_RADIUS;
}

inline constexpr float wire_segment_hit_tolerance() {
    return editor_constants::WIRE_SEGMENT_HIT_TOLERANCE;
}

inline constexpr float resize_handle_hit_radius() {
    return editor_constants::RESIZE_HANDLE_HIT_RADIUS;
}

inline constexpr float resize_handle_render_radius() {
    return editor_constants::RESIZE_HANDLE_SIZE * 0.5f;
}

inline ui::Rect centered_square(ui::Pt center, float radius) {
    return ui::Rect{
        .x = center.x - radius,
        .y = center.y - radius,
        .w = radius * 2.0f,
        .h = radius * 2.0f,
    };
}

inline ui::Rect resize_handle_hit_bounds(ui::Pt corner, ResizeCorner which) {
    const float outward = resize_handle_hit_radius();
    const float inward = resize_handle_render_radius();

    switch (which) {
        case ResizeCorner::TopLeft:
            return ui::Rect{corner.x - outward, corner.y - outward,
                            outward + inward, outward + inward};
        case ResizeCorner::TopRight:
            return ui::Rect{corner.x - inward, corner.y - outward,
                            outward + inward, outward + inward};
        case ResizeCorner::BottomLeft:
            return ui::Rect{corner.x - outward, corner.y - inward,
                            outward + inward, outward + inward};
        case ResizeCorner::BottomRight:
            return ui::Rect{corner.x - inward, corner.y - inward,
                            outward + inward, outward + inward};
    }

    return ui::Rect{};
}

inline bool point_in_rect(ui::Pt point, const ui::Rect& bounds) {
    return point.x >= bounds.x && point.x <= bounds.x + bounds.w &&
           point.y >= bounds.y && point.y <= bounds.y + bounds.h;
}

inline bool point_in_inscribed_circle(ui::Pt point, const ui::Rect& bounds) {
    const float center_x = bounds.x + bounds.w * 0.5f;
    const float center_y = bounds.y + bounds.h * 0.5f;
    const float radius = std::min(bounds.w, bounds.h) * 0.5f;
    const float dx = point.x - center_x;
    const float dy = point.y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

inline bool point_hits_shape(ui::Pt point, HitShapeKind shape, const ui::Rect& bounds) {
    switch (shape) {
        case HitShapeKind::Rectangle:
            return point_in_rect(point, bounds);
        case HitShapeKind::Circle:
            return point_in_inscribed_circle(point, bounds);
    }

    return false;
}

inline float distance(ui::Pt a, ui::Pt b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

inline float distance_to_segment(ui::Pt point, ui::Pt a, ui::Pt b) {
    const float ab_x = b.x - a.x;
    const float ab_y = b.y - a.y;
    const float len_sq = ab_x * ab_x + ab_y * ab_y;
    if (len_sq < 1e-6f) {
        return distance(point, a);
    }

    float t = ((point.x - a.x) * ab_x + (point.y - a.y) * ab_y) / len_sq;
    t = std::clamp(t, 0.0f, 1.0f);
    const ui::Pt closest(a.x + t * ab_x, a.y + t * ab_y);
    return distance(point, closest);
}

inline bool point_hits_wire_segment(ui::Pt point, ui::Pt a, ui::Pt b) {
    return distance_to_segment(point, a, b) < wire_segment_hit_tolerance();
}

inline constexpr float group_title_height() {
    return editor_constants::GROUP_TITLE_PADDING * 2.0f + editor_constants::Font::Medium;
}

inline bool point_hits_group_frame(ui::Pt point, const ui::Rect& bounds) {
    const float x0 = bounds.x;
    const float y0 = bounds.y;
    const float x1 = x0 + bounds.w;
    const float y1 = y0 + bounds.h;
    if (point.x < x0 || point.x > x1 || point.y < y0 || point.y > y1) {
        return false;
    }

    const bool on_title = point.y <= y0 + group_title_height();
    const float margin = editor_constants::GROUP_BORDER_HIT_MARGIN;
    const bool on_border = point.x <= x0 + margin || point.x >= x1 - margin || point.y >= y1 - margin;
    return on_title || on_border;
}

} // namespace editor::presentation::hit_geometry

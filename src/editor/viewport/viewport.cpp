#include "viewport/viewport.h"
#include "layout_constants.h"
#include <algorithm>
#include <cmath>

namespace {

// Доступные шаги сетки
constexpr float GRID_STEPS[] = {4.0f, 8.0f, 12.0f, 16.0f, 24.0f, 32.0f, 48.0f, 64.0f};
constexpr size_t GRID_STEPS_COUNT = sizeof(GRID_STEPS) / sizeof(GRID_STEPS[0]);

} // namespace

// [BUG-e5f6] Was 32.0f, mismatched Blueprint default of 16.0f — caused visual glitch on fresh start
Viewport::Viewport()
    : pan(Pt::zero())
    , zoom(1.0f)
    , grid_step(editor_constants::DEFAULT_GRID_STEP)
{}

Pt Viewport::screen_to_world(Pt screen, Pt canvas_min) const {
    Pt rel(screen.x - canvas_min.x, screen.y - canvas_min.y);
    return Pt(rel.x / zoom + pan.x, rel.y / zoom + pan.y);
}

Pt Viewport::world_to_screen(Pt world, Pt canvas_min) const {
    return Pt((world.x - pan.x) * zoom + canvas_min.x,
              (world.y - pan.y) * zoom + canvas_min.y);
}

void Viewport::pan_by(Pt screen_delta) {
    // При положительном delta экран двигается - viewport смещается в противоположную сторону
    pan.x -= screen_delta.x / zoom;
    pan.y -= screen_delta.y / zoom;
}

void Viewport::zoom_at(float delta, Pt screen_pos, Pt canvas_min) {
    // Запоминаем мировые координаты до zoom
    Pt world_before = screen_to_world(screen_pos, canvas_min);

    // Применяем zoom
    zoom = zoom * (1.0f + delta * editor_constants::ZOOM_SPEED);
    clamp_zoom();

    // Корректируем pan чтобы точка осталась под курсором
    Pt world_after = screen_to_world(screen_pos, canvas_min);
    pan.x -= world_after.x - world_before.x;
    pan.y -= world_after.y - world_before.y;
}

void Viewport::grid_step_up() {
    for (size_t i = 0; i < GRID_STEPS_COUNT; i++) {
        if (GRID_STEPS[i] > grid_step) {
            grid_step = GRID_STEPS[i];
            return;
        }
    }
    grid_step = GRID_STEPS[GRID_STEPS_COUNT - 1];
}

void Viewport::grid_step_down() {
    for (int i = (int)GRID_STEPS_COUNT - 1; i >= 0; i--) {
        if (GRID_STEPS[i] < grid_step) {
            grid_step = GRID_STEPS[i];
            return;
        }
    }
    grid_step = GRID_STEPS[0];
}

void Viewport::clamp_zoom() {
    if (zoom < editor_constants::ZOOM_MIN) zoom = editor_constants::ZOOM_MIN;
    if (zoom > editor_constants::ZOOM_MAX) zoom = editor_constants::ZOOM_MAX;
}

void Viewport::fit_content(Pt content_min, Pt content_max, float window_w, float window_h) {
    float cw = content_max.x - content_min.x;
    float ch = content_max.y - content_min.y;
    if (cw < 1.0f) cw = 1.0f;
    if (ch < 1.0f) ch = 1.0f;

    constexpr float padding = 60.0f; // pixels of margin
    float usable_w = std::max(window_w - 2 * padding, 1.0f);
    float usable_h = std::max(window_h - 2 * padding, 1.0f);

    zoom = std::min(usable_w / cw, usable_h / ch);
    clamp_zoom();

    // Center content: pan so that center of content maps to center of window
    float cx = (content_min.x + content_max.x) * 0.5f;
    float cy = (content_min.y + content_max.y) * 0.5f;
    pan.x = cx - (window_w * 0.5f) / zoom;
    pan.y = cy - (window_h * 0.5f) / zoom;
}

void Viewport::centerOn(Pt world_point, float window_w, float window_h) {
    pan.x = world_point.x - (window_w * 0.5f) / zoom;
    pan.y = world_point.y - (window_h * 0.5f) / zoom;
}

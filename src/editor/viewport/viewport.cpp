#include "viewport/viewport.h"

namespace {

constexpr float ZOOM_MIN = 0.25f;
constexpr float ZOOM_MAX = 4.0f;
constexpr float ZOOM_SPEED = 0.001f;

} // namespace

Viewport::Viewport()
    : pan(Pt::zero())
    , zoom(1.0f)
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
    zoom = zoom * (1.0f + delta * ZOOM_SPEED);
    clamp_zoom();

    // Корректируем pan чтобы точка осталась под курсором
    Pt world_after = screen_to_world(screen_pos, canvas_min);
    pan.x -= world_after.x - world_before.x;
    pan.y -= world_after.y - world_before.y;
}

void Viewport::clamp_zoom() {
    if (zoom < ZOOM_MIN) zoom = ZOOM_MIN;
    if (zoom > ZOOM_MAX) zoom = ZOOM_MAX;
}

#include "primitives.h"
#include "visual/renderer/draw_list.h"
#include "visual/render_context.h"

namespace visual {

float fallback_text_width(std::string_view text, float font_size) {
    if (text.empty()) return 0.0f;
    return text.length() * font_size * 0.5f;
}

// ============ Label ============

Label::Label(std::string_view text, float font_size, uint32_t color,
             TextAlign align)
    : text_(text), font_size_(font_size), color_(color), align_(align) {}

float Label::estimateWidth() const {
    return fallback_text_width(text_, font_size_);
}

Pt Label::preferredSize(IDrawList* dl) const {
    float w = dl ? dl->calc_text_size(text_.c_str(), font_size_).x : estimateWidth();
    return Pt(w, font_size_);
}

void Label::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl || text_.empty()) return;
    
    Pt pos = ctx.world_to_screen(worldPos());
    Pt sz = size();
    float zoom = ctx.zoom;

    float font = font_size_ * zoom;
    float ty = pos.y + (sz.y * zoom - font) / 2;
    float tx = pos.x;
    
    if (align_ == TextAlign::Right) {
        float text_w = dl->calc_text_size(text_.c_str(), font).x;
        tx = pos.x + sz.x * zoom - text_w;
    }
    dl->set_clip_rect(pos, Pt(pos.x + sz.x * zoom, pos.y + sz.y * zoom));
    dl->add_text(Pt(tx, ty), text_.c_str(), color_, font);
    dl->clear_clip();
}

// ============ ReservedSpace ============

ReservedSpace::ReservedSpace(Pt intrinsic_size, bool reserve_width, bool reserve_height)
    : intrinsic_size_(intrinsic_size)
    , reserve_width_(reserve_width)
    , reserve_height_(reserve_height) {}

Pt ReservedSpace::preferredSize(IDrawList*) const {
    return intrinsic_size_;
}

Pt ReservedSpace::minimumSize(IDrawList*) const {
    return Pt(reserve_width_ ? intrinsic_size_.x : 0.0f,
              reserve_height_ ? intrinsic_size_.y : 0.0f);
}

void ReservedSpace::layout(float w, float h) {
    setSize(Pt(w, h));
}

// ============ Circle ============

Circle::Circle(float radius, uint32_t color)
    : radius_(radius), color_(color) {
    setSize(Pt(radius * 2, radius * 2));
}

Pt Circle::preferredSize(IDrawList*) const {
    return Pt(radius_ * 2, radius_ * 2);
}

void Circle::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;
    
    Pt pos = ctx.world_to_screen(worldPos());
    float r = radius_ * ctx.zoom;
    
    dl->add_circle_filled(Pt(pos.x + r, pos.y + r), r, color_, 16);
}

} // namespace visual

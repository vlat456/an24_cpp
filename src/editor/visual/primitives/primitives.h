#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include <string>
#include <string_view>
#include <cstdint>

namespace visual {

enum class TextAlign { Left, Right };

float fallback_text_width(std::string_view text, float font_size);

class Label : public Widget {
public:
    Label(std::string_view text, float font_size, uint32_t color = 0xFFFFFFFF,
          TextAlign align = TextAlign::Left);

    Pt preferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;

    const std::string& text() const { return text_; }
    TextAlign align() const { return align_; }

private:
    std::string text_;
    float font_size_;
    uint32_t color_;
    TextAlign align_;

    float estimateWidth() const;
};

class Spacer : public Widget {
public:
    Spacer() { setFlexGrow(1.0f); }

    Pt preferredSize(IDrawList* dl) const override { return Pt(0, 0); }
    void render(IDrawList* dl, const RenderContext& ctx) const override {}
};

class ReservedSpace : public Widget {
public:
    ReservedSpace(Pt intrinsic_size, bool reserve_width = true, bool reserve_height = true);

    Pt preferredSize(IDrawList* dl) const override;
    Pt minimumSize(IDrawList* dl) const override;
    void layout(float w, float h) override;
    void render(IDrawList* dl, const RenderContext& ctx) const override {}

private:
    Pt intrinsic_size_;
    bool reserve_width_ = true;
    bool reserve_height_ = true;
};

class Circle : public Widget {
public:
    Circle(float radius, uint32_t color);

    Pt preferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;

private:
    float radius_;
    uint32_t color_;
};

} // namespace visual

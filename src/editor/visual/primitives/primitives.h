#pragma once
#include "visual/widget.h"
#include "visual/render_context.h"
#include <string>
#include <cstdint>

namespace visual {

enum class TextAlign { Left, Right };

class Label : public Widget {
public:
    Label(const std::string& text, float font_size, uint32_t color = 0xFFFFFFFF,
          TextAlign align = TextAlign::Left);

    Pt preferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, const RenderContext& ctx) const override;

private:
    std::string text_;
    float font_size_;
    uint32_t color_;
    TextAlign align_;

    float estimateWidth() const;
};

class Spacer : public Widget {
public:
    Spacer() { setFlexible(true); }

    Pt preferredSize(IDrawList* dl) const override { return Pt(0, 0); }
    void render(IDrawList* dl, const RenderContext& ctx) const override {}
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

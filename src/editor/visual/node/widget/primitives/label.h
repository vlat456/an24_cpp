#pragma once

#include "visual/node/widget/widget_base.h"
#include <string>
#include <cstdint>

enum class TextAlign { Left, Right };

class Label : public Widget {
public:
    Label(const std::string& text, float font_size, uint32_t color = 0xFFFFFFFF,
          TextAlign align = TextAlign::Left);

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

private:
    std::string text_;
    float font_size_;
    uint32_t color_;
    TextAlign align_;

    float estimateWidth() const;
};

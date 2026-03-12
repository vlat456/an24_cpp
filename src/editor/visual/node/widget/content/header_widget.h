#pragma once

#include "visual/node/widget/widget_base.h"
#include <string>
#include <cstdint>

class HeaderWidget : public Widget {
public:
    HeaderWidget(const std::string& name, uint32_t fill_color, float rounding = 0.0f);

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    static constexpr float HEIGHT = 24.0f;
    static constexpr float VISUAL_HEIGHT = 20.0f;

    static float estimateTextWidth(const std::string& text) {
        if (text.empty()) return 0;
        size_t codepoints = 0;
        for (size_t i = 0; i < text.size(); ) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x80) i += 1;
            else if ((c >> 5) == 0x6) i += 2;
            else if ((c >> 4) == 0xE) i += 3;
            else if ((c >> 3) == 0x1E) i += 4;
            else i += 1;
            ++codepoints;
        }
        return codepoints * FONT_SIZE * 0.6f;
    }

private:
    std::string name_;
    uint32_t fill_color_;
    float rounding_;
    static constexpr float FONT_SIZE = 12.0f;
    static constexpr float PADDING = 5.0f;
};

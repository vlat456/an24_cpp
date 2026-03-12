#pragma once

#include "visual/node/widget/widget_base.h"
#include <string>

class VoltmeterWidget : public Widget {
public:
    VoltmeterWidget(float value = 0.0f, float min_val = 0.0f, float max_val = 30.0f,
                   const std::string& unit = "V");

    void setValue(float value) { value_ = value; }
    float getValue() const { return value_; }

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    static constexpr float GAUGE_RADIUS = 40.0f;
    static constexpr float NEEDLE_LENGTH = 32.0f;

private:
    mutable float value_;
    float min_val_;
    float max_val_;
    std::string unit_;

    static constexpr float START_ANGLE = 210.0f;
    static constexpr float SWEEP_ANGLE = -240.0f;

    static constexpr uint32_t COLOR_GAUGE_BORDER = 0xFF3E3130;
    static constexpr uint32_t COLOR_NEEDLE       = 0xFF2A70C8;
    static constexpr uint32_t COLOR_TICK_MAJOR   = 0xFFDCD5D4;
    static constexpr uint32_t COLOR_TICK_MINOR   = 0xFF606070;
    static constexpr uint32_t COLOR_TEXT         = 0xFFDCD5D4;
    static constexpr float VALUE_FONT_SIZE = 14.0f;
    static constexpr float UNIT_FONT_SIZE = 10.0f;
};

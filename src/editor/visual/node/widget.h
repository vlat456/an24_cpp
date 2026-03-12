#pragma once

#include "widget_base.h"
#include <string>
#include <cstdint>

struct IDrawList;

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

class TypeNameWidget : public Widget {
public:
    explicit TypeNameWidget(const std::string& type_name);

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    static constexpr float HEIGHT = 16.0f;

private:
    std::string type_name_;
    static constexpr float FONT_SIZE = 10.0f;
};

class SwitchWidget : public Widget {
public:
    SwitchWidget(bool state = false, bool tripped = false);

    void setState(bool s) { state_ = s; }
    void setTripped(bool t) { tripped_ = t; }
    bool state() const { return state_; }
    bool tripped() const { return tripped_; }

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    static constexpr float HEIGHT = 20.0f;
    static constexpr float MIN_WIDTH = 40.0f;

private:
    mutable bool state_;
    mutable bool tripped_;
    static constexpr float FONT_SIZE = 11.0f;
    static constexpr float ROUNDING = 4.0f;
};

class VerticalToggleWidget : public Widget {
public:
    VerticalToggleWidget(bool state = false, bool tripped = false);

    void setState(bool s) { state_ = s; }
    void setTripped(bool t) { tripped_ = t; }
    bool state() const { return state_; }
    bool tripped() const { return tripped_; }

    Pt getPreferredSize(IDrawList* dl) const override;
    void render(IDrawList* dl, Pt origin, float zoom) const override;

    static constexpr float WIDTH = 16.0f;
    static constexpr float HEIGHT = 50.0f;
    static constexpr float TRACK_WIDTH = 6.0f;
    static constexpr float HANDLE_SIZE = 12.0f;

private:
    mutable bool state_;
    mutable bool tripped_;
    static constexpr float ROUNDING = 2.0f;
};

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

    static constexpr uint32_t COLOR_GAUGE_BG     = 0xFF1C1D24;
    static constexpr uint32_t COLOR_GAUGE_BORDER = 0xFF3E3130;
    static constexpr uint32_t COLOR_NEEDLE       = 0xFF2A70C8;
    static constexpr uint32_t COLOR_TICK_MAJOR   = 0xFFDCD5D4;
    static constexpr uint32_t COLOR_TICK_MINOR   = 0xFF606070;
    static constexpr uint32_t COLOR_TEXT         = 0xFFDCD5D4;
    static constexpr float VALUE_FONT_SIZE = 14.0f;
    static constexpr float UNIT_FONT_SIZE = 10.0f;
};

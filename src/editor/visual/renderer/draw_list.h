#pragma once

#include "data/pt.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/// Abstract draw list — imgui-compatible rendering interface.
struct IDrawList {
    virtual ~IDrawList() = default;

    virtual void add_line(Pt a, Pt b, uint32_t color, float thickness = 1.0f) = 0;
    virtual void add_rect(Pt min, Pt max, uint32_t color, float thickness = 1.0f) = 0;
    virtual void add_rect_with_rounding_corners(Pt min, Pt max, uint32_t color, float rounding, int corners, float thickness = 1.0f) = 0;
    virtual void add_rect_filled(Pt min, Pt max, uint32_t color) = 0;
    virtual void add_rect_filled_with_rounding(Pt min, Pt max, uint32_t color, float rounding) = 0;
    virtual void add_rect_filled_with_rounding_corners(Pt min, Pt max, uint32_t color, float rounding, int corners) = 0;
    virtual void add_circle(Pt center, float radius, uint32_t color, int segments = 12) = 0;
    virtual void add_circle_filled(Pt center, float radius, uint32_t color, int segments = 12) = 0;
    virtual void add_text(Pt pos, const char* text, uint32_t color, float font_size = 14.0f) = 0;
    virtual void add_polyline(const Pt* points, size_t count, uint32_t color, float thickness = 1.0f) = 0;
    virtual Pt calc_text_size(const char* text, float font_size) const = 0;
};

/// Tooltip info for hovered elements (port/wire).
struct TooltipInfo {
    bool active = false;
    Pt screen_pos;
    std::string text;
    std::string label;
};

#pragma once

#include <cmath>

namespace ui {

/// Точка в 2D пространстве (x, y)
/// Используется для позиций, размеров, смещений
struct Pt {
    float x;
    float y;

    Pt() : x(0.0f), y(0.0f) {}
    Pt(float x_, float y_) : x(x_), y(y_) {}

    static Pt zero() { return Pt(0.0f, 0.0f); }

    Pt operator+(const Pt& o) const { return Pt(x + o.x, y + o.y); }
    Pt operator-(const Pt& o) const { return Pt(x - o.x, y - o.y); }
    Pt operator*(float s) const { return Pt(x * s, y * s); }

    bool operator==(const Pt& o) const {
        return std::abs(x - o.x) < 1e-6f && std::abs(y - o.y) < 1e-6f;
    }

    bool operator!=(const Pt& o) const { return !(*this == o); }
};

} // namespace ui

using ui::Pt;

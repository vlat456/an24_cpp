#pragma once

namespace ui {

struct Edges {
    float left = 0, top = 0, right = 0, bottom = 0;

    static Edges all(float v) { return {v, v, v, v}; }
    static Edges symmetric(float horizontal, float vertical) {
        return {horizontal, vertical, horizontal, vertical};
    }
};

} // namespace ui

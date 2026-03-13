#pragma once

#include "ui/math/pt.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>

namespace ui {

class Widget;

class Grid {
public:
    void clear() { cells_.clear(); bounds_.clear(); }
    
    void insert(Widget* w);
    void remove(Widget* w);
    void update(Widget* w) { remove(w); insert(w); }
    
    std::vector<Widget*> query(Pt world_pos, float margin) const;
    
    template<typename T>
    std::vector<T*> queryAs(Pt world_pos, float margin) const {
        std::vector<T*> result;
        for (auto* w : query(world_pos, margin)) {
            if (auto* t = dynamic_cast<T*>(w)) result.push_back(t);
        }
        return result;
    }

private:
    static constexpr float CELL_SIZE = 64.0f;
    
    struct Cell { std::vector<Widget*> widgets; };
    std::unordered_map<int64_t, Cell> cells_;
    
    struct Bounds { int cx0, cy0, cx1, cy1; };
    std::unordered_map<Widget*, Bounds> bounds_;
    
    static int64_t key(int cx, int cy) { return (int64_t(uint32_t(cx)) << 32) | uint32_t(cy); }
    static int coord(float v) { return int(std::floor(v / CELL_SIZE)); }
    
    Bounds calcBounds(Widget* w) const;
    void insertIntoCells(Widget* w, const Bounds& b);
    void removeFromCells(Widget* w, const Bounds& b);
};

} // namespace ui

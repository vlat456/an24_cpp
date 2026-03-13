#pragma once

#include "ui/math/pt.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ui {

/// Generic spatial hash grid for fast point-based widget queries.
/// Template parameter WidgetType must provide worldMin() and worldMax().
template<typename WidgetType>
class Grid {
public:
    void clear() { cells_.clear(); bounds_.clear(); }
    
    void insert(WidgetType* w) {
        auto b = calcBounds(w);
        bounds_[w] = b;
        insertIntoCells(w, b);
    }
    
    void remove(WidgetType* w) {
        auto it = bounds_.find(w);
        if (it == bounds_.end()) return;
        removeFromCells(w, it->second);
        bounds_.erase(it);
    }
    
    void update(WidgetType* w) { remove(w); insert(w); }
    
    std::vector<WidgetType*> query(Pt world_pos, float margin) const {
        std::vector<WidgetType*> result;
        std::unordered_set<WidgetType*> seen;
        
        int cx0 = coord(world_pos.x - margin);
        int cy0 = coord(world_pos.y - margin);
        int cx1 = coord(world_pos.x + margin);
        int cy1 = coord(world_pos.y + margin);
        
        for (int cx = cx0; cx <= cx1; cx++) {
            for (int cy = cy0; cy <= cy1; cy++) {
                auto it = cells_.find(key(cx, cy));
                if (it == cells_.end()) continue;
                for (WidgetType* w : it->second.widgets) {
                    if (seen.insert(w).second) result.push_back(w);
                }
            }
        }
        return result;
    }
    
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
    
    struct Cell { std::vector<WidgetType*> widgets; };
    std::unordered_map<int64_t, Cell> cells_;
    
    struct Bounds { int cx0, cy0, cx1, cy1; };
    std::unordered_map<WidgetType*, Bounds> bounds_;
    
    static int64_t key(int cx, int cy) { return (int64_t(uint32_t(cx)) << 32) | uint32_t(cy); }
    static int coord(float v) { return int(std::floor(v / CELL_SIZE)); }
    
    Bounds calcBounds(WidgetType* w) const {
        Pt mn = w->worldMin();
        Pt mx = w->worldMax();
        return { coord(mn.x), coord(mn.y), coord(mx.x), coord(mx.y) };
    }
    
    void insertIntoCells(WidgetType* w, const Bounds& b) {
        for (int cx = b.cx0; cx <= b.cx1; cx++)
            for (int cy = b.cy0; cy <= b.cy1; cy++)
                cells_[key(cx, cy)].widgets.push_back(w);
    }
    
    void removeFromCells(WidgetType* w, const Bounds& b) {
        for (int cx = b.cx0; cx <= b.cx1; cx++) {
            for (int cy = b.cy0; cy <= b.cy1; cy++) {
                auto it = cells_.find(key(cx, cy));
                if (it == cells_.end()) continue;
                auto& vec = it->second.widgets;
                vec.erase(std::remove(vec.begin(), vec.end(), w), vec.end());
            }
        }
    }
};

} // namespace ui

#pragma once

#include "widget.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <algorithm>

namespace ui {

/// Spatial hash grid for fast point-based widget queries.
/// Works with ui::Widget* (uses worldMin() and worldMax()).
class Grid {
public:
    void clear() { cells_.clear(); bounds_.clear(); }
    
    void insert(Widget* w) {
        auto b = calcBounds(w);
        bounds_[w] = b;
        insertIntoCells(w, b);
    }
    
    void remove(Widget* w) {
        auto it = bounds_.find(w);
        if (it == bounds_.end()) return;
        removeFromCells(w, it->second);
        bounds_.erase(it);
    }
    
    void update(Widget* w) { remove(w); insert(w); }
    
    std::vector<Widget*> query(Pt world_pos, float margin) const {
        std::vector<Widget*> result;
        std::unordered_set<Widget*> seen;
        
        int cx0 = coord(world_pos.x - margin);
        int cy0 = coord(world_pos.y - margin);
        int cx1 = coord(world_pos.x + margin);
        int cy1 = coord(world_pos.y + margin);
        
        for (int cx = cx0; cx <= cx1; cx++) {
            for (int cy = cy0; cy <= cy1; cy++) {
                auto it = cells_.find(key(cx, cy));
                if (it == cells_.end()) continue;
                for (Widget* w : it->second.widgets) {
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

    /// Iterate all non-empty cells, calling fn(const std::vector<Widget*>&)
    /// for each cell's widget list. Useful for broadphase pair detection:
    /// widgets sharing a cell are spatial neighbors.
    template<typename Fn>
    void forEachCell(Fn&& fn) const {
        for (const auto& [k, cell] : cells_) {
            if (!cell.widgets.empty()) {
                fn(cell.widgets);
            }
        }
    }

private:
    static constexpr float CELL_SIZE = 64.0f;
    
    struct Cell { std::vector<Widget*> widgets; };
    std::unordered_map<int64_t, Cell> cells_;
    
    struct Bounds { int cx0, cy0, cx1, cy1; };
    std::unordered_map<Widget*, Bounds> bounds_;
    
    static int64_t key(int cx, int cy) { return (int64_t(uint32_t(cx)) << 32) | uint32_t(cy); }
    static int coord(float v) { return int(std::floor(v / CELL_SIZE)); }
    
    Bounds calcBounds(Widget* w) const {
        Pt mn = w->worldMin();
        Pt mx = w->worldMax();
        return { coord(mn.x), coord(mn.y), coord(mx.x), coord(mx.y) };
    }
    
    void insertIntoCells(Widget* w, const Bounds& b) {
        for (int cx = b.cx0; cx <= b.cx1; cx++)
            for (int cy = b.cy0; cy <= b.cy1; cy++)
                cells_[key(cx, cy)].widgets.push_back(w);
    }
    
    void removeFromCells(Widget* w, const Bounds& b) {
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

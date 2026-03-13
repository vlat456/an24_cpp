#include "grid.h"
#include "widget.h"
#include <algorithm>

namespace ui {

Grid::Bounds Grid::calcBounds(Widget* w) const {
    Pt mn = w->worldMin();
    Pt mx = w->worldMax();
    return { coord(mn.x), coord(mn.y), coord(mx.x), coord(mx.y) };
}

void Grid::insert(Widget* w) {
    auto b = calcBounds(w);
    bounds_[w] = b;
    insertIntoCells(w, b);
}

void Grid::remove(Widget* w) {
    auto it = bounds_.find(w);
    if (it == bounds_.end()) return;
    removeFromCells(w, it->second);
    bounds_.erase(it);
}

std::vector<Widget*> Grid::query(Pt pos, float margin) const {
    std::vector<Widget*> result;
    std::unordered_set<Widget*> seen;
    
    int cx0 = coord(pos.x - margin);
    int cy0 = coord(pos.y - margin);
    int cx1 = coord(pos.x + margin);
    int cy1 = coord(pos.y + margin);
    
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

void Grid::insertIntoCells(Widget* w, const Bounds& b) {
    for (int cx = b.cx0; cx <= b.cx1; cx++)
        for (int cy = b.cy0; cy <= b.cy1; cy++)
            cells_[key(cx, cy)].widgets.push_back(w);
}

void Grid::removeFromCells(Widget* w, const Bounds& b) {
    for (int cx = b.cx0; cx <= b.cx1; cx++) {
        for (int cy = b.cy0; cy <= b.cy1; cy++) {
            auto it = cells_.find(key(cx, cy));
            if (it == cells_.end()) continue;
            auto& vec = it->second.widgets;
            vec.erase(std::remove(vec.begin(), vec.end(), w), vec.end());
        }
    }
}

} // namespace ui

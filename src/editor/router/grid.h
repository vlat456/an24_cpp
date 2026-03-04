#pragma once

#include "data/pt.h"
#include "data/node.h"
#include <cmath>
#include <vector>
#include <array>
#include <functional>

/// Направление движения для ортогонального routing
enum class Dir {
    None,   // стартовая позиция
    Horiz,  // горизонтально
    Vert    // вертикально
};

/// Grid позиция
struct GridPt {
    int x, y;

    bool operator==(const GridPt& other) const {
        return x == other.x && y == other.y;
    }
};

/// Hash для GridPt
struct GridPtHash {
    size_t operator()(GridPt const& pt) const noexcept {
        return (static_cast<size_t>(pt.x) << 32) ^ static_cast<size_t>(pt.y);
    }
};

/// Конвертировать world position в grid
inline GridPt grid_from_world(Pt world, float step) {
    return GridPt{
        (int)std::round(world.x / step),
        (int)std::round(world.y / step)
    };
}

/// Конвертировать grid в world position
inline Pt grid_to_world(GridPt grid, float step) {
    return Pt(grid.x * step, grid.y * step);
}

/// 4 соседа (up, down, left, right)
inline std::array<GridPt, 4> grid_neighbors(GridPt pt) {
    return {GridPt{pt.x, pt.y + 1}, GridPt{pt.x, pt.y - 1},
            GridPt{pt.x + 1, pt.y}, GridPt{pt.x - 1, pt.y}};
}

/// State = (grid position, direction) - для turn penalty
struct State {
    GridPt pt;
    Dir dir;

    bool operator==(const State& other) const {
        return pt == other.pt && dir == other.dir;
    }
};

inline State state_new(GridPt pt, Dir dir) {
    return State{pt, dir};
}

/// Hash для State
struct StateHash {
    size_t operator()(State const& s) const noexcept {
        size_t h1 = GridPtHash{}(s.pt);
        size_t h2 = static_cast<size_t>(s.dir);
        return h1 ^ (h2 << 16);
    }
};

/// Стоимость поворота
inline float turn_cost(Dir from, Dir to) {
    if (from == Dir::None) return 0.0f;
    if (from == to) return 0.0f;
    return 3.0f; // TURN_PENALTY
}

/// Manhattan distance heuristic
inline float heuristic(GridPt a, GridPt b) {
    return (float)(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}

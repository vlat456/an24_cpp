#pragma once

#include "ui/math/pt.h"
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
        // Use proper hash combine to avoid collisions / UB on 32-bit
        size_t h = static_cast<size_t>(pt.x) * 2654435761u;
        h ^= static_cast<size_t>(pt.y) * 2246822519u;
        return h;
    }
};

/// Конвертировать world position в grid (round — для общих целей)
inline GridPt grid_from_world(Pt world, float step) {
    return GridPt{
        (int)std::round(world.x / step),
        (int)std::round(world.y / step)
    };
}

/// Конвертировать world position в grid (floor — для min-bound obstacles)
inline GridPt grid_from_world_floor(Pt world, float step) {
    return GridPt{
        (int)std::floor(world.x / step),
        (int)std::floor(world.y / step)
    };
}

/// Конвертировать world position в grid (ceil — для max-bound obstacles)
inline GridPt grid_from_world_ceil(Pt world, float step) {
    return GridPt{
        (int)std::ceil(world.x / step),
        (int)std::ceil(world.y / step)
    };
}

/// Конвертировать grid в world position
inline Pt grid_to_world(GridPt grid, float step) {
    return Pt(grid.x * step, grid.y * step);
}

// BUGFIX [71ef3b] Removed dead grid_neighbors() and state_new() — unused by A* implementation

/// State = (grid position, direction) - для turn penalty
struct State {
    GridPt pt;
    Dir dir;

    bool operator==(const State& other) const {
        return pt == other.pt && dir == other.dir;
    }
};

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
    return 15.0f; // TURN_PENALTY (high to avoid staircase)
}

/// Стоимость прыжка через перпендикулярный провод
constexpr float JUMP_OVER_COST = 5.0f;

/// Manhattan distance heuristic
inline float heuristic(GridPt a, GridPt b) {
    return (float)(std::abs(a.x - b.x) + std::abs(a.y - b.y));
}

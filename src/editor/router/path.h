#pragma once

#include "grid.h"
#include <vector>

/// Удалить collinear точки - оставить только corners (turning points)
inline std::vector<GridPt> simplify_path(const std::vector<GridPt>& path) {
    if (path.size() <= 2) return path;

    std::vector<GridPt> result;
    result.push_back(path[0]);

    // Проверяем каждую промежуточную точку
    for (size_t i = 1; i < path.size() - 1; i++) {
        const GridPt& prev = path[i - 1];
        const GridPt& curr = path[i];
        const GridPt& next = path[i + 1];

        // Проверяем - это turning point?
        // Точка является turning point если направление меняется
        // Т.е. если вектор prev->curr имеет другое направление чем curr->next

        bool prev_horiz = (prev.x != curr.x);  // движение по горизонтали
        bool next_horiz = (curr.x != next.x);  // движение по горизонтали

        bool prev_vert = (prev.y != curr.y);   // движение по вертикали
        bool next_vert = (curr.y != next.y);   // движение по вертикали

        // Turning point если направление меняется с horiz на vert или наоборот
        bool is_turn = (prev_horiz && next_vert) || (prev_vert && next_horiz);

        if (is_turn) {
            result.push_back(curr);
        }
        // Иначе - collinear, пропускаем
    }

    result.push_back(path.back());
    return result;
}

/// Конвертировать grid path в world points
inline std::vector<Pt> grid_path_to_world(const std::vector<GridPt>& grid_path, float step) {
    std::vector<Pt> world_path;
    world_path.reserve(grid_path.size());

    for (const auto& gp : grid_path) {
        world_path.push_back(grid_to_world(gp, step));
    }

    return world_path;
}

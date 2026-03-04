#pragma once

#include "algorithm.h"
#include "path.h"
#include "data/pt.h"
#include "data/node.h"
#include <vector>
#include <unordered_set>

/// Создать obstacles из nodes (с clearance)
inline std::unordered_set<GridPt, GridPtHash> make_obstacles(
    const std::vector<Node>& nodes,
    float grid_step,
    int clearance = 1) {

    std::unordered_set<GridPt, GridPtHash> obstacles;

    for (const auto& node : nodes) {
        GridPt min = grid_from_world(node.pos, grid_step);
        GridPt max = grid_from_world(Pt(node.pos.x + node.size.x, node.pos.y + node.size.y), grid_step);

        // Расширяем на clearance
        for (int x = min.x - clearance; x <= max.x + clearance; x++) {
            for (int y = min.y - clearance; y <= max.y + clearance; y++) {
                obstacles.insert(GridPt{x, y});
            }
        }
    }

    return obstacles;
}

/// Основная функция роутинга: найти путь вокруг nodes
inline std::vector<Pt> route_around_nodes(
    Pt start,
    Pt end,
    const std::vector<Node>& nodes,
    float grid_step) {

    // Конвертируем в grid
    GridPt grid_start = grid_from_world(start, grid_step);
    GridPt grid_end = grid_from_world(end, grid_step);

    // Создаем obstacles (с clearance = 1 grid step - padding вокруг nodes)
    auto obstacles = make_obstacles(nodes, grid_step, 1);

    // Убираем ТОЛЬКО порт позиции из obstacles (чтобы можно было начать/закончить)
    // НЕ убираем offset - путь должен идти вокруг padding зоны
    obstacles.erase(grid_start);
    obstacles.erase(grid_end);

    // A* поиск - ищет путь от порта к порту, но должен обходить padded nodes
    auto grid_path = astar_search(grid_start, grid_end, obstacles);

    if (!grid_path || grid_path->empty()) {
        return {}; // Путь не найден
    }

    // Упрощаем путь - оставляем только turning points
    auto simplified = simplify_path(*grid_path);

    // Конвертируем обратно в world
    return grid_path_to_world(simplified, grid_step);
}

/// L-shape fallback - простой роутинг без obstacles (с 1 grid step отступом)
inline std::vector<Pt> route_l_shape(Pt start, Pt end, float grid_step = 16.0f) {
    std::vector<Pt> path;
    path.push_back(start);

    // L-shape с отступом 1 grid step
    Pt corner(end.x, start.y + grid_step);  // чуть ниже start по Y
    path.push_back(corner);
    path.push_back(end);

    return path;
}

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

    // Создаем obstacles (с clearance = 1 grid step)
    auto obstacles = make_obstacles(nodes, grid_step);

    // Добавляем 1 grid step отступ от start и end точек
    // Это обеспечивает расстояние от портов
    GridPt grid_start_offset = grid_start;
    GridPt grid_end_offset = grid_end;

    // Смещаем отступ в направлении от центра схемы (просто +1)
    // Для более умного определения направления нужно знать направление на порт
    grid_start_offset.x += 1;
    grid_end_offset.x -= 1;

    // Убираем offset точки из obstacles (и оригинальные тоже)
    obstacles.erase(grid_start);
    obstacles.erase(grid_end);
    obstacles.erase(grid_start_offset);
    obstacles.erase(grid_end_offset);

    // A* поиск от offset точек
    auto grid_path = astar_search(grid_start_offset, grid_end_offset, obstacles);

    if (!grid_path || grid_path->empty()) {
        return {}; // Путь не найден
    }

    // Упрощаем путь - оставляем только turning points
    auto simplified = simplify_path(*grid_path);

    // Конвертируем обратно в world
    auto world_path = grid_path_to_world(simplified, grid_step);

    // Добавляем start и end точки в результат
    std::vector<Pt> result;
    result.push_back(start);  // оригинальная start позиция (порт)
    for (const auto& pt : world_path) {
        result.push_back(pt);
    }
    result.push_back(end);    // оригинальная end позиция (порт)

    return result;
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

#pragma once

#include "algorithm.h"
#include "path.h"
#include "data/pt.h"
#include "data/node.h"
#include "data/wire.h"
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

/// Build a full RoutingGrid with node obstacles + existing wire segments
inline RoutingGrid make_routing_grid(
    const std::vector<Node>& nodes,
    const std::vector<std::vector<Pt>>& existing_wire_paths,
    float grid_step,
    int clearance = 1) {

    RoutingGrid grid;

    // 1. Node obstacles (with clearance)
    for (const auto& node : nodes) {
        GridPt min = grid_from_world(node.pos, grid_step);
        GridPt max = grid_from_world(Pt(node.pos.x + node.size.x, node.pos.y + node.size.y), grid_step);

        for (int x = min.x - clearance; x <= max.x + clearance; x++) {
            for (int y = min.y - clearance; y <= max.y + clearance; y++) {
                grid.mark(GridPt{x, y}, CellNode);
            }
        }
    }

    // 2. Existing wire segments (with 1-cell padding for parallel wires)
    for (const auto& path : existing_wire_paths) {
        for (size_t i = 0; i + 1 < path.size(); i++) {
            GridPt a = grid_from_world(path[i], grid_step);
            GridPt b = grid_from_world(path[i + 1], grid_step);

            bool is_horiz = (a.y == b.y);
            bool is_vert  = (a.x == b.x);
            uint8_t flag = is_horiz ? CellWireH : (is_vert ? CellWireV : 0);

            if (flag == 0) continue;  // diagonal — skip

            // Mark cells along the segment
            int steps = std::max(std::abs(b.x - a.x), std::abs(b.y - a.y));
            if (steps == 0) continue;
            int dx = (b.x > a.x) ? 1 : (b.x < a.x ? -1 : 0);
            int dy = (b.y > a.y) ? 1 : (b.y < a.y ? -1 : 0);
            int cx = a.x, cy = a.y;
            for (int s = 0; s <= steps; s++) {
                grid.mark(GridPt{cx, cy}, flag);
                // Wire padding: mark adjacent cells for parallel wire avoidance
                if (is_horiz) {
                    grid.mark(GridPt{cx, cy + 1}, CellWireH);
                    grid.mark(GridPt{cx, cy - 1}, CellWireH);
                } else {
                    grid.mark(GridPt{cx + 1, cy}, CellWireV);
                    grid.mark(GridPt{cx - 1, cy}, CellWireV);
                }
                cx += dx;
                cy += dy;
            }
        }
    }

    return grid;
}

/// Основная функция роутинга: найти путь вокруг nodes
inline std::vector<Pt> route_around_nodes(
    Pt start,
    Pt end,
    const std::vector<Node>& nodes,
    float grid_step,
    const std::vector<std::vector<Pt>>& existing_wire_paths = {}) {

    // Конвертируем в grid
    GridPt grid_start = grid_from_world(start, grid_step);
    GridPt grid_end = grid_from_world(end, grid_step);

    // Создаем routing grid (nodes + existing wires)
    auto grid = make_routing_grid(nodes, existing_wire_paths, grid_step);

    // Убираем start/end из obstacles (порты могут быть на ноде)
    grid.cells.erase(grid_start);
    grid.cells.erase(grid_end);

    // A* поиск
    auto grid_path = astar_search(grid_start, grid_end, grid);

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

/// L-shape fallback - простой роутинг без obstacles
inline std::vector<Pt> route_l_shape(Pt start, Pt end, float /*grid_step*/ = 16.0f) {
    std::vector<Pt> path;
    path.push_back(start);

    // L-shape: horizontal then vertical
    Pt corner(end.x, start.y);
    if (!(corner == start) && !(corner == end)) {
        path.push_back(corner);
    }
    path.push_back(end);

    return path;
}

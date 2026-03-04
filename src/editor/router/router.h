#pragma once

#include "algorithm.h"
#include "path.h"
#include "data/pt.h"
#include "data/node.h"
#include "data/wire.h"
#include "data/port.h"
#include <vector>
#include <unordered_set>

/// Создать obstacles из nodes (с clearance)
inline std::unordered_set<GridPt, GridPtHash> make_obstacles(
    const std::vector<Node>& nodes,
    float grid_step,
    int clearance = 1) {

    std::unordered_set<GridPt, GridPtHash> obstacles;

    for (const auto& node : nodes) {
        GridPt min = grid_from_world_floor(node.pos, grid_step);
        GridPt max = grid_from_world_ceil(Pt(node.pos.x + node.size.x, node.pos.y + node.size.y), grid_step);

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
        GridPt min = grid_from_world_floor(node.pos, grid_step);
        GridPt max = grid_from_world_ceil(Pt(node.pos.x + node.size.x, node.pos.y + node.size.y), grid_step);

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

/// Determine departure direction for a port (which way the wire should leave)
/// Returns a unit offset in grid coordinates: {+1,0} for outputs, {-1,0} for inputs, etc.
struct PortDir { int dx, dy; };

inline PortDir get_port_departure(const Node& node, const char* port_name) {
    // Bus: no preferred direction
    if (node.kind == NodeKind::Bus) {
        return {0, 0};
    }
    // Ref: port is on top → wire departs upward (negative Y)
    if (node.kind == NodeKind::Ref) {
        return {0, -1};
    }
    // Normal node: inputs on left face → depart left, outputs on right face → depart right
    for (const auto& p : node.inputs) {
        if (p.name == port_name) return {-1, 0};
    }
    for (const auto& p : node.outputs) {
        if (p.name == port_name) return {+1, 0};
    }
    return {0, 0};  // unknown port
}

/// Основная функция роутинга: найти путь вокруг nodes
/// start_node/end_node are needed for port departure direction
inline std::vector<Pt> route_around_nodes(
    Pt start,
    Pt end,
    const Node& start_node,
    const char* start_port,
    const Node& end_node,
    const char* end_port,
    const std::vector<Node>& nodes,
    float grid_step,
    const std::vector<std::vector<Pt>>& existing_wire_paths = {}) {

    // Конвертируем порт позиции в grid
    GridPt grid_start = grid_from_world(start, grid_step);
    GridPt grid_end = grid_from_world(end, grid_step);

    // Departure/arrival: 1 grid cell away from port in port's natural direction
    PortDir start_dir = get_port_departure(start_node, start_port);
    PortDir end_dir   = get_port_departure(end_node, end_port);

    GridPt grid_start_dep = {grid_start.x + start_dir.dx, grid_start.y + start_dir.dy};
    GridPt grid_end_arr   = {grid_end.x + end_dir.dx, grid_end.y + end_dir.dy};

    // If port has no preferred direction (Bus), use the port cell itself
    if (start_dir.dx == 0 && start_dir.dy == 0) grid_start_dep = grid_start;
    if (end_dir.dx == 0 && end_dir.dy == 0) grid_end_arr = grid_end;

    // Создаем routing grid (nodes + existing wires)
    constexpr int clearance = 1;
    auto grid = make_routing_grid(nodes, existing_wire_paths, grid_step, clearance);

    // Carve corridor: port → departure → 1 extra cell beyond (to connect to free space)
    // This ensures A* can reach the departure/arrival cells even when
    // they fall inside the obstacle clearance zone (floor/ceil boundaries).
    auto carve_corridor = [&](GridPt port, GridPt dep, PortDir dir) {
        grid.cells.erase(port);
        grid.cells.erase(dep);
        // Clear cells beyond departure (outward from node) until outside obstacle zone
        for (int step = 1; step <= clearance + 1; step++) {
            grid.cells.erase(GridPt{dep.x + dir.dx * step, dep.y + dir.dy * step});
        }
    };

    if (start_dir.dx != 0 || start_dir.dy != 0) {
        carve_corridor(grid_start, grid_start_dep, start_dir);
    } else {
        grid.cells.erase(grid_start);
        grid.cells.erase(grid_start_dep);
    }

    if (end_dir.dx != 0 || end_dir.dy != 0) {
        carve_corridor(grid_end, grid_end_arr, end_dir);
    } else {
        grid.cells.erase(grid_end);
        grid.cells.erase(grid_end_arr);
    }

    // A* поиск from departure to arrival
    auto grid_path = astar_search(grid_start_dep, grid_end_arr, grid);

    if (!grid_path || grid_path->empty()) {
        return {}; // Путь не найден
    }

    // Упрощаем путь - оставляем только turning points
    auto simplified = simplify_path(*grid_path);

    // Конвертируем обратно в world
    auto world_path = grid_path_to_world(simplified, grid_step);

    // Build result: start port → departure → A* path → arrival → end port
    std::vector<Pt> result;
    result.push_back(start);  // оригинальная start позиция (порт)

    // Add departure point if it differs from first A* point
    Pt dep_world = grid_to_world(grid_start_dep, grid_step);
    if (!world_path.empty() && !(world_path.front() == dep_world)) {
        result.push_back(dep_world);
    }

    for (const auto& pt : world_path) {
        result.push_back(pt);
    }

    // Add arrival point if it differs from last A* point
    Pt arr_world = grid_to_world(grid_end_arr, grid_step);
    if (!world_path.empty() && !(world_path.back() == arr_world)) {
        result.push_back(arr_world);
    }

    result.push_back(end);    // оригинальная end позиция (порт)

    return result;
}

/// Simplified overload without port info (uses grid-only routing)
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

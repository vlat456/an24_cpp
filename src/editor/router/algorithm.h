#pragma once

#include "grid.h"
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <vector>
#include <optional>
#include <cmath>
#include <array>

/// Cell flags for the routing grid
enum CellFlag : uint8_t {
    CellEmpty  = 0,
    CellNode   = 1,  ///< Node obstacle (with padding)
    CellWireH  = 2,  ///< Horizontal wire segment
    CellWireV  = 4,  ///< Vertical wire segment
};

/// Routing grid with cell flags
struct RoutingGrid {
    std::unordered_map<GridPt, uint8_t, GridPtHash> cells;

    void mark(GridPt pt, uint8_t flag) {
        cells[pt] |= flag;
    }

    uint8_t get(GridPt pt) const {
        auto it = cells.find(pt);
        return it != cells.end() ? it->second : CellEmpty;
    }

    bool is_blocked(GridPt pt) const {
        return (get(pt) & CellNode) != 0;
    }

    /// Cost to enter this cell from given direction (jump-over perpendicular wires)
    float wire_crossing_cost(GridPt pt, Dir enter_dir) const {
        uint8_t flags = get(pt);
        if (flags == CellEmpty) return 0.0f;
        // Crossing a perpendicular wire costs extra
        if (enter_dir == Dir::Horiz && (flags & CellWireV)) return JUMP_OVER_COST;
        if (enter_dir == Dir::Vert  && (flags & CellWireH)) return JUMP_OVER_COST;
        // Parallel wire in same cell is blocked (wire padding)
        if (enter_dir == Dir::Horiz && (flags & CellWireH)) return 100.0f;
        if (enter_dir == Dir::Vert  && (flags & CellWireV)) return 100.0f;
        return 0.0f;
    }
};

/// A* поиск пути с direction-aware состояниями и turn penalty
inline std::optional<std::vector<GridPt>> astar_search(
    GridPt start, GridPt goal,
    const RoutingGrid& grid) {

    struct AStarNode {
        State state;
        float g_cost;
        float f_cost;
        int parent_idx;  // index into closed_list, -1 = no parent

        bool operator>(const AStarNode& other) const {
            return f_cost > other.f_cost; // min-heap
        }
    };

    // Pool of closed nodes for path reconstruction
    std::vector<AStarNode> closed_list;
    closed_list.reserve(1024);

    std::unordered_map<State, float, StateHash> g_scores;
    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> open_set;

    State initial{start, Dir::None};
    g_scores[initial] = 0.0f;
    open_set.push({initial, 0.0f, heuristic(start, goal), -1});

    int iterations = 0;
    const int MAX_ITERATIONS = 100000;

    while (!open_set.empty() && iterations < MAX_ITERATIONS) {
        iterations++;
        AStarNode current = open_set.top();
        open_set.pop();

        // Skip if we already found a better path to this state
        auto it = g_scores.find(current.state);
        if (it != g_scores.end() && current.g_cost > it->second + 0.0001f) {
            continue;
        }

        // Store in closed list for path reconstruction
        int current_idx = (int)closed_list.size();
        closed_list.push_back(current);

        if (current.state.pt == goal) {
            // Reconstruct path via indices
            std::vector<GridPt> path;
            int idx = current_idx;
            while (idx >= 0) {
                path.push_back(closed_list[idx].state.pt);
                idx = closed_list[idx].parent_idx;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        // 4 directions: Right, Left, Up, Down
        struct Move { int dx, dy; Dir dir; };
        static constexpr Move moves[] = {
            {+1,  0, Dir::Horiz},
            {-1,  0, Dir::Horiz},
            { 0, +1, Dir::Vert},
            { 0, -1, Dir::Vert},
        };

        for (const auto& [dx, dy, dir] : moves) {
            GridPt next{current.state.pt.x + dx, current.state.pt.y + dy};
            if (grid.is_blocked(next)) continue;

            State next_state{next, dir};
            float turn = turn_cost(current.state.dir, dir);
            float wire_extra = grid.wire_crossing_cost(next, dir);
            float tg = current.g_cost + 1.0f + turn + wire_extra;

            auto nit = g_scores.find(next_state);
            if (nit == g_scores.end() || tg < nit->second - 0.0001f) {
                g_scores[next_state] = tg;
                float f = tg + heuristic(next, goal);
                open_set.push({next_state, tg, f, current_idx});
            }
        }
    }

    return std::nullopt;
}

/// Backward-compatible overload using obstacle set (no wire cost)
inline std::optional<std::vector<GridPt>> astar_search(
    GridPt start, GridPt goal,
    const std::unordered_set<GridPt, GridPtHash>& obstacles) {

    RoutingGrid grid;
    for (const auto& pt : obstacles) {
        grid.mark(pt, CellNode);
    }
    return astar_search(start, goal, grid);
}

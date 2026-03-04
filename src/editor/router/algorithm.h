#pragma once

#include "grid.h"
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <vector>
#include <optional>
#include <cmath>
#include <array>

/// A* поиск пути с direction-aware состояниями и turn penalty
inline std::optional<std::vector<GridPt>> astar_search(
    GridPt start, GridPt goal,
    const std::unordered_set<GridPt, GridPtHash>& obstacles) {

    struct Node {
        State state;
        float g_cost;
        float f_cost;
        const Node* parent;

        bool operator<(const Node& other) const {
            return f_cost > other.f_cost; // min-heap
        }
    };

    std::unordered_map<State, float, StateHash> g_scores;
    std::priority_queue<Node> open_set;

    State initial{start, Dir::None};
    g_scores[initial] = 0.0f;
    open_set.push({initial, 0.0f, heuristic(start, goal), nullptr});

    int iterations = 0;
    const int MAX_ITERATIONS = 100000;

    while (!open_set.empty() && iterations < MAX_ITERATIONS) {
        iterations++;
        Node current = open_set.top();
        open_set.pop();

        if (current.state.pt == goal) {
            // Восстанавливаем путь
            std::vector<GridPt> path;
            const Node* node = &current;
            while (node) {
                path.push_back(node->state.pt);
                node = node->parent;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        auto it = g_scores.find(current.state);
        if (it != g_scores.end() && current.g_cost > it->second + 0.0001f) {
            continue;
        }

        // 4 направления
        GridPt pt = current.state.pt;
        Dir current_dir = current.state.dir;

        // Right (Horiz)
        {
            GridPt next{pt.x + 1, pt.y};
            if (!obstacles.count(next)) {
                State next_state{next, Dir::Horiz};
                float turn = turn_cost(current_dir, Dir::Horiz);
                float tg = current.g_cost + 1.0f + turn;

                auto nit = g_scores.find(next_state);
                if (nit == g_scores.end() || tg < nit->second - 0.0001f) {
                    g_scores[next_state] = tg;
                    float f = tg + heuristic(next, goal);
                    open_set.push({next_state, tg, f, new Node(current)});
                }
            }
        }
        // Left (Horiz)
        {
            GridPt next{pt.x - 1, pt.y};
            if (!obstacles.count(next)) {
                State next_state{next, Dir::Horiz};
                float turn = turn_cost(current_dir, Dir::Horiz);
                float tg = current.g_cost + 1.0f + turn;

                auto nit = g_scores.find(next_state);
                if (nit == g_scores.end() || tg < nit->second - 0.0001f) {
                    g_scores[next_state] = tg;
                    float f = tg + heuristic(next, goal);
                    open_set.push({next_state, tg, f, new Node(current)});
                }
            }
        }
        // Up (Vert)
        {
            GridPt next{pt.x, pt.y + 1};
            if (!obstacles.count(next)) {
                State next_state{next, Dir::Vert};
                float turn = turn_cost(current_dir, Dir::Vert);
                float tg = current.g_cost + 1.0f + turn;

                auto nit = g_scores.find(next_state);
                if (nit == g_scores.end() || tg < nit->second - 0.0001f) {
                    g_scores[next_state] = tg;
                    float f = tg + heuristic(next, goal);
                    open_set.push({next_state, tg, f, new Node(current)});
                }
            }
        }
        // Down (Vert)
        {
            GridPt next{pt.x, pt.y - 1};
            if (!obstacles.count(next)) {
                State next_state{next, Dir::Vert};
                float turn = turn_cost(current_dir, Dir::Vert);
                float tg = current.g_cost + 1.0f + turn;

                auto nit = g_scores.find(next_state);
                if (nit == g_scores.end() || tg < nit->second - 0.0001f) {
                    g_scores[next_state] = tg;
                    float f = tg + heuristic(next, goal);
                    open_set.push({next_state, tg, f, new Node(current)});
                }
            }
        }
    }

    return std::nullopt;
}

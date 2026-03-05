#pragma once

#include "node.h"
#include "wire.h"
#include "pt.h"
#include <vector>

/// Blueprint - схема соединений (все домены: электрика, гидравлика, механика)
///
/// В отличие от Circuit - более общее название.
/// Содержит узлы и провода (wires) между портами.
struct Blueprint {
    /// Все узлы в схеме
    std::vector<Node> nodes;

    /// Все провода (соединения между портами)
    std::vector<Wire> wires;

    /// Viewport состояние (pan/zoom) - сохраняется с схемой
    Pt pan;
    float zoom;
    float grid_step;

    /// [f6g7h8i9] Monotonic counter for generating unique wire IDs
    int next_wire_id = 0;

    Blueprint()
        : pan(Pt::zero())
        , zoom(1.0f)
        , grid_step(16.0f)
    {}

    /// Добавить узел
    size_t add_node(Node node) {
        size_t idx = nodes.size();
        nodes.push_back(std::move(node));
        return idx;
    }

    /// Добавить провод
    size_t add_wire(Wire wire) {
        size_t idx = wires.size();
        wires.push_back(std::move(wire));
        return idx;
    }

    /// Найти узел по ID
    Node* find_node(const char* id) {
        for (auto& n : nodes) {
            if (n.id == id) return &n;
        }
        return nullptr;
    }
};

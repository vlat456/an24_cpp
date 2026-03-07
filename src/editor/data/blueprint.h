#pragma once

#include "node.h"
#include "wire.h"
#include "pt.h"
#include "../../json_parser/json_parser.h"
#include <vector>
#include <string>

/// Collapsed blueprint group - editor-only metadata for visual hierarchy
/// When a blueprint is expanded in the simulation, we still want to show it as collapsed
/// in the editor until the user drills down. This tracks that visual state.
struct CollapsedGroup {
    std::string id;              // Unique ID for this collapsed group (e.g., "lamp1")
    std::string blueprint_path;  // Path to blueprint JSON file
    std::string type_name;       // Component type name (e.g., "lamp_pass_through")
    Pt pos;                      // Visual position of collapsed node
    Pt size;                     // Visual size of collapsed node
    std::vector<std::string> internal_node_ids;  // IDs of internal devices in this group

    // Constructor
    CollapsedGroup() : pos(Pt::zero()), size(Pt(120.0f, 80.0f)) {}

    CollapsedGroup(const std::string& id_, const std::string& path, const std::string& type)
        : id(id_), blueprint_path(path), type_name(type), pos(Pt::zero()), size(Pt(120.0f, 80.0f)) {}
};

/// Blueprint - схема соединений (все домены: электрика, гидравлика, механика)
///
/// В отличие от Circuit - более общее название.
/// Содержит узлы и провода (wires) между портами.
struct Blueprint {
    /// Все узлы в схеме
    std::vector<Node> nodes;

    /// Все провода (соединения между портами)
    std::vector<Wire> wires;

    /// Editor-only metadata: visually collapsed blueprint groups
    /// These are NOT separate devices - they're just visual groupings of existing nodes
    std::vector<CollapsedGroup> collapsed_groups;

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

    /// Добавить провод с проверкой совместимости типов портов
    /// Возвращает true если провод добавлен, false если типы несовместимы
    bool add_wire_validated(Wire wire);

    /// Найти узел по ID
    Node* find_node(const char* id) {
        for (auto& n : nodes) {
            if (n.id == id) return &n;
        }
        return nullptr;
    }
};

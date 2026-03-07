#pragma once

#include "node.h"
#include "wire.h"
#include "pt.h"
#include "../../json_parser/json_parser.h"
#include <vector>
#include <string>
#include <set>

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

    /// Добавить узел (returns index, or existing index if ID already present)
    size_t add_node(Node node) {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].id == node.id) return i;
        }
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
    const Node* find_node(const char* id) const {
        for (const auto& n : nodes) {
            if (n.id == id) return &n;
        }
        return nullptr;
    }

    /// Recompute visibility of all nodes from collapsed_groups + drill stack.
    /// Supports N-level deep hierarchical blueprints.
    ///
    /// `drill_stack` is the navigation path: empty = top-level,
    /// ["A"] = drilled into A, ["A", "A:sub"] = drilled into sub inside A.
    ///
    /// Algorithm:
    ///   1. Collect ALL internal node IDs across all groups → hidden by default.
    ///   2. For each expanded (drilled-into) group, mark its internals as visible.
    ///   3. For each non-expanded group, re-hide its internals (they're behind
    ///      their own collapsed node). This handles sub-groups correctly:
    ///      drilling into A shows A's children, but A:sub's children stay hidden
    ///      behind the collapsed A:sub node.
    ///   4. Blueprint collapsed nodes: visible unless drilled into.
    void recompute_visibility(const std::vector<std::string>& drill_stack = {}) {
        // Set of groups that are expanded (drilled into)
        std::set<std::string> expanded(drill_stack.begin(), drill_stack.end());

        // Collect all internal node IDs across all groups
        std::set<std::string> all_internal;
        for (const auto& g : collapsed_groups)
            for (const auto& id : g.internal_node_ids)
                all_internal.insert(id);

        // Build the set of visible internal nodes:
        // Start with internals of expanded groups, then subtract internals
        // of non-expanded sub-groups (those remain collapsed).
        std::set<std::string> visible_internals;
        for (const auto& group_id : expanded) {
            for (const auto& g : collapsed_groups) {
                if (g.id == group_id) {
                    for (const auto& id : g.internal_node_ids)
                        visible_internals.insert(id);
                    break;
                }
            }
        }
        // Remove internals of non-expanded groups (they're behind collapsed nodes)
        for (const auto& g : collapsed_groups) {
            if (expanded.count(g.id) == 0) {
                for (const auto& id : g.internal_node_ids)
                    visible_internals.erase(id);
            }
        }

        for (auto& n : nodes) {
            if (n.kind == NodeKind::Blueprint) {
                // Collapsed Blueprint node: visible only when NOT drilled into
                // AND either top-level or reachable (visible in parent group)
                bool not_expanded = expanded.count(n.id) == 0;
                bool is_reachable = !all_internal.count(n.id) || visible_internals.count(n.id) > 0;
                n.visible = not_expanded && is_reachable;
            } else if (all_internal.count(n.id)) {
                // Internal node: visible only if exposed by drill stack
                n.visible = visible_internals.count(n.id) > 0;
            } else {
                // Top-level regular node: always visible
                n.visible = true;
            }
        }
    }
};

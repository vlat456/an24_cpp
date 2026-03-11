#pragma once

#include "node.h"
#include "wire.h"
#include "pt.h"
#include "layout_constants.h"
#include "../../json_parser/json_parser.h"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>

/// Instance of a sub-blueprint — reference (baked_in=false) or embedded (baked_in=true).
struct SubBlueprintInstance {
    std::string id;                  // Unique instance ID: "lamp_1"
    std::string blueprint_path;      // "library/systems/lamp_pass_through.blueprint"
    std::string type_name;           // "lamp_pass_through" (for UI display)

    bool baked_in = false;           // true = inline devices saved to JSON
                                     // false = expand from library file on load

    Pt pos = Pt::zero();             // Layout of collapsed node
    Pt size = Pt(editor_constants::SUB_BLUEPRINT_DEFAULT_WIDTH, editor_constants::SUB_BLUEPRINT_DEFAULT_HEIGHT);

    std::map<std::string, std::string> params_override;
    std::map<std::string, Pt> layout_override;
    std::map<std::string, std::vector<Pt>> internal_routing;

    std::vector<std::string> internal_node_ids;

    SubBlueprintInstance() = default;

    SubBlueprintInstance(const std::string& id_, const std::string& path, const std::string& type)
        : id(id_), blueprint_path(path), type_name(type) {}
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

    /// [2.1] O(1) wire dedup index — mirrors wires vector
    std::unordered_set<WireKey, WireKeyHash> wire_index_;

    /// [2.1] Rebuild wire_index_ from wires vector (after bulk modifications)
    void rebuild_wire_index() {
        wire_index_.clear();
        wire_index_.reserve(wires.size());
        for (const auto& w : wires) {
            wire_index_.insert(WireKey(w));
        }
    }

    /// Sub-blueprint instances (unified: baked_in flag controls reference vs embedded)
    std::vector<SubBlueprintInstance> sub_blueprint_instances;

    /// Find sub-blueprint instance by ID
    SubBlueprintInstance* find_sub_blueprint_instance(const std::string& id);
    const SubBlueprintInstance* find_sub_blueprint_instance(const std::string& id) const;

    /// Remove sub-blueprint instance by ID (returns true if removed)
    bool remove_sub_blueprint_instance(const std::string& id);

    /// Convert a SubBlueprintInstance from reference (baked_in=false) to embedded (baked_in=true).
    /// Merges overrides into actual node data, sets baked_in flag, clears overrides.
    /// Returns false if id not found in sub_blueprint_instances or already baked in.
    bool bake_in_sub_blueprint(const std::string& id);

    /// Viewport состояние (pan/zoom) - сохраняется с схемой
    Pt pan;
    float zoom;
    float grid_step;

    /// [f6g7h8i9] Monotonic counter for generating unique wire IDs
    int next_wire_id = 0;

    Blueprint()
        : pan(Pt::zero())
        , zoom(1.0f)
        , grid_step(editor_constants::DEFAULT_GRID_STEP)
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

    /// Добавить провод (with runtime dedup guard)
    size_t add_wire(Wire wire);

    /// Добавить провод с проверкой совместимости типов портов
    /// Возвращает true если провод добавлен, false если типы несовместимы
    [[nodiscard]] bool add_wire_validated(Wire wire);

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

    /// Recompute group_id for all nodes from sub_blueprint_instances.
    /// Internal nodes of a sub-blueprint get group_id = sbi.id.
    /// Top-level nodes (not in any group) get group_id = "".
    /// Collapsed Blueprint nodes themselves get group_id = their parent group
    /// (empty if top-level).
    void recompute_group_ids() {
        // Build reverse map: node_id → group_id
        std::unordered_map<std::string, std::string> node_to_group;
        for (const auto& g : sub_blueprint_instances) {
            for (const auto& id : g.internal_node_ids) {
                node_to_group[id] = g.id;
            }
        }

        for (auto& n : nodes) {
            auto it = node_to_group.find(n.id);
            if (it != node_to_group.end()) {
                n.group_id = it->second;
            } else if (!n.expandable) {
                // Non-expandable nodes not in any group are top-level.
                // Expandable (collapsed) nodes keep their group_id as-is
                // because it was set explicitly when added to a sub-window.
                n.group_id = "";
            }
        }
    }

    /// Collect all node IDs that belong to a sub-blueprint (recursively).
    /// If a sub-blueprint contains other expandable nodes, their internal nodes are included too.
    /// Also collects the sub-blueprint IDs themselves (for cleanup).
    void collect_group_internals(const std::string& group_id,
                                 std::unordered_set<std::string>& out_node_ids,
                                 std::unordered_set<std::string>& out_group_ids) const {
        // Find the SubBlueprintInstance for this group_id
        for (const auto& g : sub_blueprint_instances) {
            if (g.id != group_id) continue;
            out_group_ids.insert(g.id);
            for (const auto& nid : g.internal_node_ids) {
                out_node_ids.insert(nid);
                // If this internal node is itself a Blueprint, recurse
                const Node* n = find_node(nid.c_str());
                if (n && n->expandable) {
                    collect_group_internals(nid, out_node_ids, out_group_ids);
                }
            }
            break;
        }
    }

    /// Auto-layout nodes belonging to a specific group_id.
    /// Assigns positions using a simple topological column layout.
    void auto_layout_group(const std::string& group_id);
};

/// Expand a TypeDefinition (blueprint) into a Blueprint with Nodes + Wires.
/// Uses stored positions/routing_points when available; falls back to TypeRegistry for ports.
/// This is the single code path for turning a TypeDefinition into editor-ready Nodes/Wires.
Blueprint expand_type_definition(const an24::TypeDefinition& def, const an24::TypeRegistry& registry);

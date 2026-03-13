#pragma once

#include "node.h"
#include "wire.h"
#include "../../ui/math/pt.h"
#include "layout_constants.h"
#include "../../json_parser/json_parser.h"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <optional>

using ui::Pt;

struct FlatBlueprint;  // Forward declaration

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

    /// O(1) node lookup by ID: node_id → index in nodes vector.
    /// Maintained by add_node(), rebuild_node_index().
    std::unordered_map<std::string, size_t> node_index_;

    /// Все провода (соединения между портами)
    std::vector<Wire> wires;

    /// [2.1] O(1) wire dedup index — mirrors wires vector
    std::unordered_set<WireKey, WireKeyHash> wire_index_;

    /// O(1) wire lookup by ID: wire_id → index in wires vector.
    /// Maintained by add_wire(), remove_wire_at(), rebuild_wire_id_index().
    std::unordered_map<std::string, size_t> wire_id_index_;

    /// [2.1] Rebuild wire_index_ from wires vector (after bulk modifications)
    void rebuild_wire_index() {
        wire_index_.clear();
        wire_index_.reserve(wires.size());
        for (const auto& w : wires) {
            wire_index_.insert(WireKey(w));
        }
    }

    /// Rebuild wire_id_index_ from wires vector (after bulk modifications).
    void rebuild_wire_id_index() {
        wire_id_index_.clear();
        wire_id_index_.reserve(wires.size());
        for (size_t i = 0; i < wires.size(); ++i) {
            wire_id_index_[wires[i].id] = i;
        }
    }

    /// Index: bus node ID → wire IDs touching that bus.
    /// Used by recreate_bus_wires to avoid O(N) scan of all wires.
    /// Wire IDs are stored (not indices) to avoid shift problems on removal.
    std::unordered_map<std::string, std::vector<std::string>> bus_wire_index_;

    /// O(1) port occupancy index: (node_id, port_name) → set of wire IDs.
    /// Used by add_wire_validated to avoid O(N) scan for occupied ports.
    /// Tracks ALL ports (Bus/RefNode included); caller decides whether to allow multiple.
    std::unordered_map<PortKey, std::unordered_set<std::string>, PortKeyHash> port_occupancy_index_;

    /// Get all wire IDs that touch a bus node. O(1) lookup.
    const std::vector<std::string>& busWires(const std::string& bus_id) const {
        static const std::vector<std::string> empty;
        auto it = bus_wire_index_.find(bus_id);
        return it != bus_wire_index_.end() ? it->second : empty;
    }

    /// Rebuild bus_wire_index_ from scratch (after bulk operations).
    void rebuild_bus_wire_index();

    /// Rebuild port_occupancy_index_ from scratch (after bulk operations).
    void rebuild_port_occupancy_index() {
        port_occupancy_index_.clear();
        for (const auto& w : wires) {
            addToPortOccupancy(w);
        }
    }

    /// Check if a port is occupied by any wire. O(1).
    bool is_port_occupied(const std::string& node_id, const std::string& port_name) const {
        auto it = port_occupancy_index_.find(PortKey(node_id, port_name));
        return it != port_occupancy_index_.end() && !it->second.empty();
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

    /// Rebuild node_index_ from nodes vector (after bulk modifications).
    void rebuild_node_index() {
        node_index_.clear();
        node_index_.reserve(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            node_index_[nodes[i].id] = i;
        }
    }

    /// Добавить узел (returns index, or existing index if ID already present)
    size_t add_node(Node node) {
        auto it = node_index_.find(node.id);
        if (it != node_index_.end()) return it->second;
        size_t idx = nodes.size();
        node_index_[node.id] = idx;
        nodes.push_back(std::move(node));
        return idx;
    }

    /// Добавить провод (with runtime dedup guard)
    size_t add_wire(Wire wire);

    /// Добавить провод с проверкой совместимости типов портов
    /// Возвращает true если провод добавлен, false если типы несовместимы
    [[nodiscard]] bool add_wire_validated(Wire wire);

    /// Remove a wire by index. Erases from both wires vector and dedup index.
    /// Returns the removed wire (for caller to inspect endpoints, etc.).
    Wire remove_wire_at(size_t index);

    /// Update the dedup key for a wire at the given index.
    /// Call this after modifying a wire's endpoints in-place.
    /// Removes the old key (computed from old_wire) and inserts the new key.
    void rekey_wire(const Wire& old_wire, const Wire& new_wire);

    /// Update bus_wire_index_ when a wire's endpoints change.
    /// Call after modifying a wire's endpoints in-place (e.g., reconnect).
    /// Removes wire ID from old bus entries, adds to new bus entries.
    void updateBusIndexForEndpoints(const Wire& old_wire, const Wire& new_wire);

    /// Update port_occupancy_index_ when a wire's endpoints change.
    /// Call after modifying a wire's endpoints in-place (e.g., reconnect).
    void updatePortOccupancyForEndpoints(const Wire& old_wire, const Wire& new_wire);

    /// Number of wires in the dedup index (for testing).
    size_t wire_index_size() const { return wire_index_.size(); }

    /// Find wire by ID (O(1) via wire_id_index_).
    Wire* find_wire(const std::string& id) {
        auto it = wire_id_index_.find(id);
        if (it == wire_id_index_.end()) return nullptr;
        return &wires[it->second];
    }
    const Wire* find_wire(const std::string& id) const {
        auto it = wire_id_index_.find(id);
        if (it == wire_id_index_.end()) return nullptr;
        return &wires[it->second];
    }

private:
    /// Add a wire's ID to bus_wire_index_ if it touches a bus node.
    void addToBusIndex(const Wire& w);
    /// Remove a wire's ID from bus_wire_index_ if it touches a bus node.
    void removeFromBusIndex(const Wire& w);

    /// Add a wire's ID to port_occupancy_index_ for both endpoints.
    void addToPortOccupancy(const Wire& w) {
        port_occupancy_index_[PortKey(w.start.node_id, w.start.port_name)].insert(w.id);
        port_occupancy_index_[PortKey(w.end.node_id, w.end.port_name)].insert(w.id);
    }
    /// Remove a wire's ID from port_occupancy_index_ for both endpoints.
    void removeFromPortOccupancy(const Wire& w) {
        auto remove_entry = [&](const std::string& node_id, const std::string& port_name) {
            auto it = port_occupancy_index_.find(PortKey(node_id, port_name));
            if (it != port_occupancy_index_.end()) {
                it->second.erase(w.id);
                if (it->second.empty()) port_occupancy_index_.erase(it);
            }
        };
        remove_entry(w.start.node_id, w.start.port_name);
        remove_entry(w.end.node_id, w.end.port_name);
    }

public:

    /// Найти узел по ID (O(1) via node_index_)
    Node* find_node(const char* id) {
        auto it = node_index_.find(id);
        if (it == node_index_.end()) return nullptr;
        return &nodes[it->second];
    }
    const Node* find_node(const char* id) const {
        auto it = node_index_.find(id);
        if (it == node_index_.end()) return nullptr;
        return &nodes[it->second];
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

    // == Serialization ==
    
    /// Serialize to editor JSON format (flat v2 schema)
    std::string serialize() const;
    
    /// Deserialize from editor JSON format
    static std::optional<Blueprint> deserialize(const std::string& json);
    
    /// Serialize to simulator JSON format (rewrites wires, skips Blueprint nodes)
    std::string to_simulator_json() const;
    
    // == Flat conversion (internal) ==
    
    FlatBlueprint to_flat() const;
    static std::optional<Blueprint> from_flat(const FlatBlueprint& flat);
};

/// Expand a TypeDefinition (blueprint) into a Blueprint with Nodes + Wires.
/// Uses stored positions/routing_points when available; falls back to TypeRegistry for ports.
/// This is the single code path for turning a TypeDefinition into editor-ready Nodes/Wires.
Blueprint expand_type_definition(const TypeDefinition& def, const TypeRegistry& registry);

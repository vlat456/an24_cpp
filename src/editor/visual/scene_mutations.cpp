#include "scene_mutations.h"
#include "scene.h"
#include "node/node_factory.h"
#include "wire/wire.h"
#include "wire/routing_point.h"
#include <algorithm>
#include <cassert>
#include <unordered_set>

namespace visual::mutations {

// ============================================================================
// Helpers (file-local)
// ============================================================================

/// Resolve a WireEnd (data) to a visual::Port* in the scene.
/// For bus nodes, the port is selected by wire_id (alias port).
static Port* resolve_port(Scene& scene, const ::WireEnd& we,
                          const std::string& wire_id) {
    auto* widget = scene.find(we.node_id);
    if (!widget) return nullptr;
    return widget->portByName(we.port_name, wire_id);
}

/// Create a visual::Wire widget from a data Wire and add it to the scene.
/// Returns the added Wire widget, or nullptr if ports could not be resolved.
static visual::Wire* create_wire_widget(Scene& scene, const ::Wire& data_wire) {
    // Validate that both endpoint ports exist before creating the widget
    Port* start_port = resolve_port(scene, data_wire.start, data_wire.id);
    Port* end_port = resolve_port(scene, data_wire.end, data_wire.id);
    if (!start_port || !end_port) return nullptr;

    // Create the Wire widget (stores endpoint IDs, resolves positions dynamically)
    auto wire_widget = std::make_unique<visual::Wire>(
        data_wire.id,
        data_wire.start.node_id, data_wire.start.port_name,
        data_wire.end.node_id, data_wire.end.port_name);
    auto* wire_ptr = wire_widget.get();

    // Add routing points as children of the wire
    for (size_t i = 0; i < data_wire.routing_points.size(); ++i) {
        wire_widget->addRoutingPoint(data_wire.routing_points[i], i);
    }

    scene.add(std::move(wire_widget));
    return wire_ptr;
}

/// Remove a visual::Wire widget from the scene by id.
static void destroy_wire_widget(Scene& scene, const std::string& wire_id) {
    auto* w = scene.find(wire_id);
    if (!w) return;
    scene.remove(w);
}

/// Recreate visual wire widgets for all wires connected to a bus node.
/// Must be called after rebuildPorts() on a bus, because port rebuild
/// may change alias port mappings.
/// \p exclude_wire_id  Skip this wire (caller handles it separately).
/// \p already_recreated  Set of wire IDs that were already recreated by a
///    prior call (e.g. when both endpoints are bus nodes). Pass-through to
///    avoid creating duplicate Wire widgets in roots_. Newly recreated
///    wire IDs are inserted into this set.
/// Uses bus_wire_index_ for O(1) lookup instead of scanning all wires.
static void recreate_bus_wires(Scene& scene, const Blueprint& bp,
                               const std::string& bus_node_id,
                               const std::string& exclude_wire_id = {},
                               std::unordered_set<std::string>* already_recreated = nullptr) {
    // Copy wire IDs to avoid holding a reference into bus_wire_index_
    // across scene mutations (widget destructors could theoretically
    // trigger Blueprint modifications in the future).
    const auto wire_ids = bp.busWires(bus_node_id);  // copy
    
    // Phase 1: Destroy orphaned wire widgets (skip wires already recreated
    // by a prior call — they are live and valid, not orphaned).
    {
        auto guard = scene.flushGuard();
        for (const auto& wid : wire_ids) {
            if (wid == exclude_wire_id) continue;
            if (already_recreated && already_recreated->count(wid)) continue;
            destroy_wire_widget(scene, wid);
        }
    }

    // Phase 2: Recreate wire widgets from Blueprint data via O(1) lookup
    for (const auto& wid : wire_ids) {
        if (wid == exclude_wire_id) continue;
        if (already_recreated && already_recreated->count(wid)) continue;
        const auto* w = bp.find_wire(wid);
        if (w) {
            create_wire_widget(scene, *w);
            if (already_recreated) already_recreated->insert(wid);
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void rebuild(Scene& scene, const Blueprint& bp, const std::string& group_id) {
    auto guard = scene.flushGuard();
    
    scene.clear();

    // 1) Create node widgets for all nodes in this group
    for (const auto& node : bp.nodes) {
        if (node.group_id != group_id) continue;
        auto widget = NodeFactory::create(node, bp.wires);
        scene.add(std::move(widget));
    }

    // 2) Create wire widgets for wires whose both endpoints are in this group
    for (const auto& wire : bp.wires) {
        const Node* sn = bp.find_node(wire.start.node_id.c_str());
        const Node* en = bp.find_node(wire.end.node_id.c_str());
        if (!sn || !en) continue;
        if (sn->group_id != group_id || en->group_id != group_id) continue;

        create_wire_widget(scene, wire);
    }
}

size_t add_node(Scene& scene, Blueprint& bp, Node node,
                const std::string& group_id) {
    node.group_id = group_id;
    size_t idx = bp.add_node(std::move(node));

    auto widget = NodeFactory::create(bp.nodes[idx], bp.wires);
    scene.add(std::move(widget));

    return idx;
}

void remove_nodes(Scene& scene, Blueprint& bp,
                  const std::vector<size_t>& indices) {
    // Collect all node IDs to delete (including recursive group internals)
    std::unordered_set<std::string> deleted_ids;
    std::unordered_set<std::string> deleted_group_ids;

    for (size_t idx : indices) {
        if (idx >= bp.nodes.size()) continue;
        const auto& node = bp.nodes[idx];
        deleted_ids.insert(node.id);
        if (node.expandable) {
            bp.collect_group_internals(node.id, deleted_ids, deleted_group_ids);
        }
    }

    // Collect wire IDs that touch deleted nodes (for scene cleanup)
    std::vector<std::string> wire_ids_to_remove;
    for (const auto& w : bp.wires) {
        if (deleted_ids.count(w.start.node_id) || deleted_ids.count(w.end.node_id)) {
            wire_ids_to_remove.push_back(w.id);
        }
    }

    // Remove wires from scene first (before nodes, so WireEnd destructors
    // find their parent ports still alive)
    {
        auto guard = scene.flushGuard();
        for (const auto& wid : wire_ids_to_remove) {
            destroy_wire_widget(scene, wid);
        }
    }

    // Remove node widgets from scene
    {
        auto guard = scene.flushGuard();
        for (const auto& nid : deleted_ids) {
            auto* w = scene.find(nid);
            if (w) scene.remove(w);
        }
    }

    // Mutate Blueprint data: remove nodes
    for (int i = static_cast<int>(bp.nodes.size()) - 1; i >= 0; --i) {
        if (deleted_ids.count(bp.nodes[static_cast<size_t>(i)].id)) {
            bp.nodes.erase(bp.nodes.begin() + i);
        }
    }

    // Remove wires touching deleted nodes
    bp.wires.erase(
        std::remove_if(bp.wires.begin(), bp.wires.end(),
            [&deleted_ids](const ::Wire& w) {
                return deleted_ids.count(w.start.node_id) ||
                       deleted_ids.count(w.end.node_id);
            }),
        bp.wires.end());

    // Remove sub-blueprint instances
    bp.sub_blueprint_instances.erase(
        std::remove_if(bp.sub_blueprint_instances.begin(),
                       bp.sub_blueprint_instances.end(),
            [&deleted_ids, &deleted_group_ids](const SubBlueprintInstance& g) {
                return deleted_group_ids.count(g.id) || deleted_ids.count(g.id);
            }),
        bp.sub_blueprint_instances.end());

    // Clean up internal_node_ids in remaining groups
    for (auto& g : bp.sub_blueprint_instances) {
        g.internal_node_ids.erase(
            std::remove_if(g.internal_node_ids.begin(), g.internal_node_ids.end(),
                [&deleted_ids](const std::string& id) {
                    return deleted_ids.count(id);
                }),
            g.internal_node_ids.end());
    }

    bp.rebuild_node_index();
    bp.rebuild_wire_index();
    bp.rebuild_wire_id_index();
    bp.rebuild_bus_wire_index();
    bp.rebuild_port_occupancy_index();
}

bool add_wire(Scene& scene, Blueprint& bp, ::Wire wire,
              const std::string& group_id) {
    // Validate both endpoints exist and belong to this group
    const Node* sn = bp.find_node(wire.start.node_id.c_str());
    const Node* en = bp.find_node(wire.end.node_id.c_str());
    if (!sn || !en) return false;
    if (sn->group_id != group_id || en->group_id != group_id) return false;

    // Notify bus nodes before validation (they need alias ports for dedup).
    // connectWire → rebuildPorts orphans existing wire widgets on the bus.
    std::string bus_start_id, bus_end_id;
    auto* start_widget = scene.find(wire.start.node_id);
    auto* end_widget = scene.find(wire.end.node_id);
    if (auto* bus = dynamic_cast<BusNodeWidget*>(start_widget)) {
        bus_start_id = wire.start.node_id;
        bus->connectWire(wire);
    }
    if (auto* bus = dynamic_cast<BusNodeWidget*>(end_widget)) {
        bus_end_id = wire.end.node_id;
        bus->connectWire(wire);
    }

    // Save wire data for potential rollback (wire will be moved)
    const ::Wire wire_copy = wire;

    // Add to Blueprint with validation (dedup, type compatibility)
    bool ok = bp.add_wire_validated(std::move(wire));
    if (!ok) {
        // Rollback bus alias ports (use copy since wire was moved-from)
        if (auto* bus = dynamic_cast<BusNodeWidget*>(start_widget))
            bus->disconnectWire(wire_copy);
        if (auto* bus = dynamic_cast<BusNodeWidget*>(end_widget))
            bus->disconnectWire(wire_copy);
        // Recreate orphaned wires after rollback.
        // Use a shared set to avoid double-creating wires that touch both buses.
        if (!bus_start_id.empty() || !bus_end_id.empty()) {
            std::unordered_set<std::string> already_recreated;
            if (!bus_start_id.empty())
                recreate_bus_wires(scene, bp, bus_start_id, {}, &already_recreated);
            if (!bus_end_id.empty() && bus_end_id != bus_start_id)
                recreate_bus_wires(scene, bp, bus_end_id, {}, &already_recreated);
        }
        return false;
    }

    const auto& added_wire = bp.wires.back();

    // Create visual wire widget for the new wire
    create_wire_widget(scene, added_wire);

    // Recreate orphaned wire widgets on affected bus nodes.
    // Use a shared set to avoid double-creating wires that touch both buses.
    if (!bus_start_id.empty() || !bus_end_id.empty()) {
        std::unordered_set<std::string> already_recreated;
        if (!bus_start_id.empty())
            recreate_bus_wires(scene, bp, bus_start_id, added_wire.id, &already_recreated);
        if (!bus_end_id.empty() && bus_end_id != bus_start_id)
            recreate_bus_wires(scene, bp, bus_end_id, added_wire.id, &already_recreated);
    }

    return true;
}

void remove_wire(Scene& scene, Blueprint& bp, size_t index) {
    if (index >= bp.wires.size()) return;

    ::Wire copy = bp.wires[index];

    // Remove visual wire widget
    {
        auto guard = scene.flushGuard();
        destroy_wire_widget(scene, copy.id);
    }

    // Notify bus nodes to remove alias ports.
    // rebuildPorts() orphans surviving wire widgets — recreate them below.
    std::string bus_start_id, bus_end_id;
    auto* start_widget = scene.find(copy.start.node_id);
    auto* end_widget = scene.find(copy.end.node_id);
    if (auto* bus = dynamic_cast<BusNodeWidget*>(start_widget)) {
        bus_start_id = copy.start.node_id;
        bus->disconnectWire(copy);
    }
    if (auto* bus = dynamic_cast<BusNodeWidget*>(end_widget)) {
        bus_end_id = copy.end.node_id;
        bus->disconnectWire(copy);
    }

    // Remove from Blueprint
    bp.remove_wire_at(index);

    // Recreate orphaned wire widgets on affected bus nodes.
    // Use a shared set to avoid double-creating wires that touch both buses.
    if (!bus_start_id.empty() || !bus_end_id.empty()) {
        std::unordered_set<std::string> already_recreated;
        if (!bus_start_id.empty())
            recreate_bus_wires(scene, bp, bus_start_id, {}, &already_recreated);
        if (!bus_end_id.empty() && bus_end_id != bus_start_id)
            recreate_bus_wires(scene, bp, bus_end_id, {}, &already_recreated);
    }
}

void reconnect_wire(Scene& scene, Blueprint& bp,
                    size_t wire_idx, bool reconnect_start, ::WireEnd new_end,
                    const std::string& group_id) {
    if (wire_idx >= bp.wires.size()) return;

    auto& wire = bp.wires[wire_idx];
    std::string wire_id = wire.id;

    // Notify old bus node to remove alias port.
    // rebuildPorts() clears WireEnd back-pointers (no cascade), but
    // orphans the surviving wire widgets — we recreate them below.
    const ::WireEnd& old_end = reconnect_start ? wire.start : wire.end;
    std::string old_bus_id;
    auto* old_widget = scene.find(old_end.node_id);
    if (auto* bus = dynamic_cast<BusNodeWidget*>(old_widget)) {
        old_bus_id = old_end.node_id;
        bus->disconnectWire(wire);
    }

    // Destroy old visual wire
    {
        auto guard = scene.flushGuard();
        destroy_wire_widget(scene, wire_id);
    }

    // Recreate orphaned wire widgets on the old bus (ports were rebuilt).
    // Track recreated wires to avoid duplicates if the new bus shares wires.
    std::unordered_set<std::string> already_recreated;
    if (!old_bus_id.empty())
        recreate_bus_wires(scene, bp, old_bus_id, wire_id, &already_recreated);

    // Update Blueprint data
    ::Wire old_wire = wire;  // snapshot before modification
    if (reconnect_start)
        wire.start = new_end;
    else
        wire.end = new_end;
    wire.routing_points.clear();
    bp.rekey_wire(old_wire, wire);
    bp.updateBusIndexForEndpoints(old_wire, wire);
    bp.updatePortOccupancyForEndpoints(old_wire, wire);

    // Notify new bus node to add alias port
    std::string new_bus_id;
    auto* new_widget = scene.find(new_end.node_id);
    if (auto* bus = dynamic_cast<BusNodeWidget*>(new_widget)) {
        new_bus_id = new_end.node_id;
        bus->connectWire(wire);
    }

    // Recreate visual wire for the reconnected wire
    create_wire_widget(scene, wire);

    // Recreate orphaned wire widgets on the new bus (if different from old)
    if (!new_bus_id.empty() && new_bus_id != old_bus_id)
        recreate_bus_wires(scene, bp, new_bus_id, wire_id, &already_recreated);
}

bool swap_wire_ports_on_bus(Scene& scene, Blueprint& bp,
                            const std::string& bus_node_id,
                            const std::string& wire_id_a,
                            const std::string& wire_id_b) {
    if (wire_id_a == wire_id_b) return false;  // no-op: same wire

    // Find wire indices in Blueprint via O(1) index lookup
    const auto* wa = bp.find_wire(wire_id_a);
    const auto* wb = bp.find_wire(wire_id_b);
    if (!wa || !wb) return false;

    // find_wire returns a pointer into bp.wires; compute indices from that
    size_t idx_a = static_cast<size_t>(wa - bp.wires.data());
    size_t idx_b = static_cast<size_t>(wb - bp.wires.data());

    // Find the bus widget and swap its alias ports.
    // swapAliasPorts → rebuildPorts orphans existing wire widgets.
    auto* bus_widget = dynamic_cast<BusNodeWidget*>(scene.find(bus_node_id));
    if (!bus_widget) return false;
    if (!bus_widget->swapAliasPorts(wire_id_a, wire_id_b)) return false;

    // Swap wires in Blueprint to maintain order consistency
    std::swap(bp.wires[idx_a], bp.wires[idx_b]);
    bp.rebuild_wire_id_index();

    // Recreate orphaned wire widgets on the bus
    recreate_bus_wires(scene, bp, bus_node_id);
    return true;
}

} // namespace visual::mutations

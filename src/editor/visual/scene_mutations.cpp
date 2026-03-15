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
                          const ui::StringInterner& interner,
                          ui::InternedId wire_id) {
    std::string_view node_sv = interner.resolve(we.node_id);
    auto* widget = scene.find(node_sv);
    if (!widget) return nullptr;
    std::string_view port_sv = interner.resolve(we.port_name);
    std::string_view wire_sv = interner.resolve(wire_id);
    return widget->portByName(port_sv, wire_sv);
}

/// Create a visual::Wire widget from a data Wire and add it to the scene.
/// Returns the added Wire widget, or nullptr if ports could not be resolved.
static visual::Wire* create_wire_widget(Scene& scene, const ::Wire& data_wire,
                                        const ui::StringInterner& interner) {
    // Validate that both endpoint ports exist before creating the widget
    Port* start_port = resolve_port(scene, data_wire.start, interner, data_wire.id);
    Port* end_port = resolve_port(scene, data_wire.end, interner, data_wire.id);
    if (!start_port || !end_port) return nullptr;

    // Resolve InternedId → string_view from the interner's stable storage
    std::string_view wire_id_sv   = interner.resolve(data_wire.id);
    std::string_view start_node_sv = interner.resolve(data_wire.start.node_id);
    std::string_view start_port_sv = interner.resolve(data_wire.start.port_name);
    std::string_view end_node_sv   = interner.resolve(data_wire.end.node_id);
    std::string_view end_port_sv   = interner.resolve(data_wire.end.port_name);

    // Create the Wire widget (stores endpoint IDs, resolves positions dynamically)
    auto wire_widget = std::make_unique<visual::Wire>(
        wire_id_sv,
        start_node_sv, start_port_sv,
        end_node_sv, end_port_sv);
    auto* wire_ptr = wire_widget.get();

    // Add routing points as children of the wire
    for (size_t i = 0; i < data_wire.routing_points.size(); ++i) {
        wire_widget->addRoutingPoint(data_wire.routing_points[i], i);
    }

    scene.add(std::move(wire_widget));
    return wire_ptr;
}

/// Remove a visual::Wire widget from the scene by id.
static void destroy_wire_widget(Scene& scene, std::string_view wire_id) {
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
                               ui::InternedId bus_node_iid,
                               ui::InternedId exclude_wire_iid = {},
                               std::unordered_set<ui::InternedId>* already_recreated = nullptr) {
    const auto& interner = bp.interner();

    // Copy wire IDs to avoid holding a reference into bus_wire_index_
    // across scene mutations (widget destructors could theoretically
    // trigger Blueprint modifications in the future).
    const auto wire_ids = bp.busWires(bus_node_iid);  // copy
    
    // Phase 1: Destroy orphaned wire widgets (skip wires already recreated
    // by a prior call — they are live and valid, not orphaned).
    {
        auto guard = scene.flushGuard();
        for (const auto& wid : wire_ids) {
            if (wid == exclude_wire_iid) continue;
            if (already_recreated && already_recreated->count(wid)) continue;
            std::string_view wid_sv = interner.resolve(wid);
            destroy_wire_widget(scene, wid_sv);
        }
    }

    // Phase 2: Recreate wire widgets from Blueprint data via O(1) lookup
    for (const auto& wid : wire_ids) {
        if (wid == exclude_wire_iid) continue;
        if (already_recreated && already_recreated->count(wid)) continue;
        const auto* w = bp.find_wire(wid);
        if (w) {
            create_wire_widget(scene, *w, interner);
            if (already_recreated) already_recreated->insert(wid);
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void rebuild(Scene& scene, const Blueprint& bp, const std::string& group_id) {
    auto guard = scene.flushGuard();
    const auto& interner = bp.interner();
    
    scene.clear();

    // 1) Create node widgets for all nodes in this group
    for (const auto& node : bp.nodes) {
        if (node.group_id != group_id) continue;
        auto widget = NodeFactory::create(node, interner, bp.wires);
        scene.add(std::move(widget));
    }

    // 2) Create wire widgets for wires whose both endpoints are in this group
    for (const auto& wire : bp.wires) {
        const Node* sn = bp.find_node(wire.start.node_id);
        const Node* en = bp.find_node(wire.end.node_id);
        if (!sn || !en) continue;
        if (sn->group_id != group_id || en->group_id != group_id) continue;

        create_wire_widget(scene, wire, interner);
    }
}

size_t add_node(Scene& scene, Blueprint& bp, Node node,
                const std::string& group_id) {
    node.group_id = group_id;
    size_t idx = bp.add_node(std::move(node));

    const auto& interner = bp.interner();
    auto widget = NodeFactory::create(bp.nodes[idx], interner, bp.wires);
    scene.add(std::move(widget));

    return idx;
}

void remove_nodes(Scene& scene, Blueprint& bp,
                  const std::vector<size_t>& indices) {
    const auto& interner = bp.interner();

    // Collect all node IDs to delete (including recursive group internals).
    // We use both InternedId sets (for data-layer comparisons) and string
    // sets (for group_internals API which still uses strings).
    std::unordered_set<ui::InternedId> deleted_iids;
    std::unordered_set<std::string> deleted_ids_str;
    std::unordered_set<std::string> deleted_group_ids;

    for (size_t idx : indices) {
        if (idx >= bp.nodes.size()) continue;
        const auto& node = bp.nodes[idx];
        deleted_iids.insert(node.id);
        std::string nid_str(interner.resolve(node.id));
        deleted_ids_str.insert(nid_str);
        if (node.expandable) {
            bp.collect_group_internals(nid_str, deleted_ids_str, deleted_group_ids);
        }
    }

    // Sync: intern all recursively-collected string IDs into deleted_iids
    // so the wire/node erasure loops (which check deleted_iids) also cover
    // internal nodes of sub-blueprints.
    for (const auto& s : deleted_ids_str) {
        deleted_iids.insert(interner.lookup(s));
    }

    // Collect wire IDs that touch deleted nodes (for scene cleanup)
    std::vector<ui::InternedId> wire_iids_to_remove;
    for (const auto& w : bp.wires) {
        if (deleted_iids.count(w.start.node_id) || deleted_iids.count(w.end.node_id)) {
            wire_iids_to_remove.push_back(w.id);
        }
    }

    // Remove wires from scene first (before nodes, so WireEnd destructors
    // find their parent ports still alive)
    {
        auto guard = scene.flushGuard();
        for (const auto& wid : wire_iids_to_remove) {
            std::string_view wid_sv = interner.resolve(wid);
            destroy_wire_widget(scene, wid_sv);
        }
    }

    // Remove node widgets from scene
    {
        auto guard = scene.flushGuard();
        for (const auto& nid_str : deleted_ids_str) {
            auto* w = scene.find(nid_str);
            if (w) scene.remove(w);
        }
    }

    // Mutate Blueprint data: remove nodes
    for (int i = static_cast<int>(bp.nodes.size()) - 1; i >= 0; --i) {
        if (deleted_iids.count(bp.nodes[static_cast<size_t>(i)].id)) {
            bp.nodes.erase(bp.nodes.begin() + i);
        }
    }

    // Remove wires touching deleted nodes
    bp.wires.erase(
        std::remove_if(bp.wires.begin(), bp.wires.end(),
            [&deleted_iids](const ::Wire& w) {
                return deleted_iids.count(w.start.node_id) ||
                       deleted_iids.count(w.end.node_id);
            }),
        bp.wires.end());

    // Remove sub-blueprint instances
    bp.sub_blueprint_instances.erase(
        std::remove_if(bp.sub_blueprint_instances.begin(),
                       bp.sub_blueprint_instances.end(),
            [&deleted_ids_str, &deleted_group_ids](const SubBlueprintInstance& g) {
                return deleted_group_ids.count(g.id) || deleted_ids_str.count(g.id);
            }),
        bp.sub_blueprint_instances.end());

    // Clean up internal_node_ids in remaining groups
    for (auto& g : bp.sub_blueprint_instances) {
        g.internal_node_ids.erase(
            std::remove_if(g.internal_node_ids.begin(), g.internal_node_ids.end(),
                [&deleted_ids_str](const std::string& id) {
                    return deleted_ids_str.count(id);
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
    const auto& interner = bp.interner();

    // Validate both endpoints exist and belong to this group
    const Node* sn = bp.find_node(wire.start.node_id);
    const Node* en = bp.find_node(wire.end.node_id);
    if (!sn || !en) return false;
    if (sn->group_id != group_id || en->group_id != group_id) return false;

    // Notify bus nodes before validation (they need alias ports for dedup).
    // connectWire → rebuildPorts orphans existing wire widgets on the bus.
    ui::InternedId bus_start_iid, bus_end_iid;
    std::string_view start_node_sv = interner.resolve(wire.start.node_id);
    std::string_view end_node_sv = interner.resolve(wire.end.node_id);
    auto* start_widget = scene.find(start_node_sv);
    auto* end_widget = scene.find(end_node_sv);
    if (auto* bus = dynamic_cast<BusNodeWidget*>(start_widget)) {
        bus_start_iid = wire.start.node_id;
        bus->connectWire(wire);
    }
    if (auto* bus = dynamic_cast<BusNodeWidget*>(end_widget)) {
        bus_end_iid = wire.end.node_id;
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
        if (!bus_start_iid.empty() || !bus_end_iid.empty()) {
            std::unordered_set<ui::InternedId> already_recreated;
            if (!bus_start_iid.empty())
                recreate_bus_wires(scene, bp, bus_start_iid, {}, &already_recreated);
            if (!bus_end_iid.empty() && bus_end_iid != bus_start_iid)
                recreate_bus_wires(scene, bp, bus_end_iid, {}, &already_recreated);
        }
        return false;
    }

    const auto& added_wire = bp.wires.back();

    // Create visual wire widget for the new wire
    create_wire_widget(scene, added_wire, interner);

    // Recreate orphaned wire widgets on affected bus nodes.
    // Use a shared set to avoid double-creating wires that touch both buses.
    if (!bus_start_iid.empty() || !bus_end_iid.empty()) {
        std::unordered_set<ui::InternedId> already_recreated;
        if (!bus_start_iid.empty())
            recreate_bus_wires(scene, bp, bus_start_iid, added_wire.id, &already_recreated);
        if (!bus_end_iid.empty() && bus_end_iid != bus_start_iid)
            recreate_bus_wires(scene, bp, bus_end_iid, added_wire.id, &already_recreated);
    }

    return true;
}

void remove_wire(Scene& scene, Blueprint& bp, size_t index) {
    if (index >= bp.wires.size()) return;

    const auto& interner = bp.interner();
    ::Wire copy = bp.wires[index];

    // Remove visual wire widget
    {
        auto guard = scene.flushGuard();
        std::string_view wire_sv = interner.resolve(copy.id);
        destroy_wire_widget(scene, wire_sv);
    }

    // Notify bus nodes to remove alias ports.
    // rebuildPorts() orphans surviving wire widgets — recreate them below.
    ui::InternedId bus_start_iid, bus_end_iid;
    std::string_view start_node_sv = interner.resolve(copy.start.node_id);
    std::string_view end_node_sv = interner.resolve(copy.end.node_id);
    auto* start_widget = scene.find(start_node_sv);
    auto* end_widget = scene.find(end_node_sv);
    if (auto* bus = dynamic_cast<BusNodeWidget*>(start_widget)) {
        bus_start_iid = copy.start.node_id;
        bus->disconnectWire(copy);
    }
    if (auto* bus = dynamic_cast<BusNodeWidget*>(end_widget)) {
        bus_end_iid = copy.end.node_id;
        bus->disconnectWire(copy);
    }

    // Remove from Blueprint
    bp.remove_wire_at(index);

    // Recreate orphaned wire widgets on affected bus nodes.
    // Use a shared set to avoid double-creating wires that touch both buses.
    if (!bus_start_iid.empty() || !bus_end_iid.empty()) {
        std::unordered_set<ui::InternedId> already_recreated;
        if (!bus_start_iid.empty())
            recreate_bus_wires(scene, bp, bus_start_iid, {}, &already_recreated);
        if (!bus_end_iid.empty() && bus_end_iid != bus_start_iid)
            recreate_bus_wires(scene, bp, bus_end_iid, {}, &already_recreated);
    }
}

void reconnect_wire(Scene& scene, Blueprint& bp,
                    size_t wire_idx, bool reconnect_start, ::WireEnd new_end,
                    const std::string& group_id) {
    if (wire_idx >= bp.wires.size()) return;

    const auto& interner = bp.interner();
    auto& wire = bp.wires[wire_idx];
    ui::InternedId wire_iid = wire.id;
    std::string_view wire_id_sv = interner.resolve(wire_iid);

    // Notify old bus node to remove alias port.
    // rebuildPorts() clears WireEnd back-pointers (no cascade), but
    // orphans the surviving wire widgets — we recreate them below.
    const ::WireEnd& old_end = reconnect_start ? wire.start : wire.end;
    ui::InternedId old_bus_iid;
    std::string_view old_end_sv = interner.resolve(old_end.node_id);
    auto* old_widget = scene.find(old_end_sv);
    if (auto* bus = dynamic_cast<BusNodeWidget*>(old_widget)) {
        old_bus_iid = old_end.node_id;
        bus->disconnectWire(wire);
    }

    // Destroy old visual wire
    {
        auto guard = scene.flushGuard();
        destroy_wire_widget(scene, wire_id_sv);
    }

    // Recreate orphaned wire widgets on the old bus (ports were rebuilt).
    // Track recreated wires to avoid duplicates if the new bus shares wires.
    std::unordered_set<ui::InternedId> already_recreated;
    if (!old_bus_iid.empty())
        recreate_bus_wires(scene, bp, old_bus_iid, wire_iid, &already_recreated);

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
    ui::InternedId new_bus_iid;
    std::string_view new_end_sv = interner.resolve(new_end.node_id);
    auto* new_widget = scene.find(new_end_sv);
    if (auto* bus = dynamic_cast<BusNodeWidget*>(new_widget)) {
        new_bus_iid = new_end.node_id;
        bus->connectWire(wire);
    }

    // Recreate visual wire for the reconnected wire
    create_wire_widget(scene, wire, interner);

    // Recreate orphaned wire widgets on the new bus (if different from old)
    if (!new_bus_iid.empty() && new_bus_iid != old_bus_iid)
        recreate_bus_wires(scene, bp, new_bus_iid, wire_iid, &already_recreated);
}

bool swap_wire_ports_on_bus(Scene& scene, Blueprint& bp,
                            ui::InternedId bus_node_iid,
                            ui::InternedId wire_id_a,
                            ui::InternedId wire_id_b) {
    if (wire_id_a == wire_id_b) return false;  // no-op: same wire

    // Find wire indices in Blueprint via O(1) index lookup
    const auto* wa = bp.find_wire(wire_id_a);
    const auto* wb = bp.find_wire(wire_id_b);
    if (!wa || !wb) return false;

    // find_wire returns a pointer into bp.wires; compute indices from that
    size_t idx_a = static_cast<size_t>(wa - bp.wires.data());
    size_t idx_b = static_cast<size_t>(wb - bp.wires.data());

    const auto& interner = bp.interner();
    std::string_view bus_sv = interner.resolve(bus_node_iid);

    // Find the bus widget and swap its alias ports.
    // swapAliasPorts → rebuildPorts orphans existing wire widgets.
    auto* bus_widget = dynamic_cast<BusNodeWidget*>(scene.find(bus_sv));
    if (!bus_widget) return false;
    if (!bus_widget->swapAliasPorts(wire_id_a, wire_id_b)) return false;

    // Swap wires in Blueprint to maintain order consistency
    std::swap(bp.wires[idx_a], bp.wires[idx_b]);
    bp.rebuild_wire_id_index();

    // Recreate orphaned wire widgets on the bus
    recreate_bus_wires(scene, bp, bus_node_iid);
    return true;
}

} // namespace visual::mutations

#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual/node/node.h"
#include "visual/hittest.h"
#include "visual/renderer/blueprint_renderer.h"
#include "visual/trigonometry.h"
#include "visual/scene/persist.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <unordered_set>

/// Scene graph: references shared blueprint, owns visual cache and viewport.
/// Single authority for hit testing, rendering, and scene mutations.
/// Each scene renders a specific nesting level identified by group_id.
class VisualScene {
public:
    /// Construct a scene viewing a shared blueprint, filtered by group_id.
    explicit VisualScene(Blueprint& bp, const std::string& group_id = "")
        : bp_(&bp), group_id_(group_id) { assert(bp_); }

    // ---- Group filtering ----

    /// Which group this scene renders. Empty = root level.
    const std::string& groupId() const { return group_id_; }
    void setGroupId(const std::string& id) { group_id_ = id; }

    /// Check if a node belongs to this scene's group
    bool ownsNode(const Node& n) const { return n.group_id == group_id_; }

    /// Check if a wire belongs to this scene (both endpoints in this group)
    bool ownsWire(const Wire& w) const {
        const Node* sn = bp_->find_node(w.start.node_id.c_str());
        const Node* en = bp_->find_node(w.end.node_id.c_str());
        return sn && en && sn->group_id == group_id_ && en->group_id == group_id_;
    }

    // ---- Data access ----

    Blueprint& blueprint() { assert(bp_); return *bp_; }
    const Blueprint& blueprint() const { assert(bp_); return *bp_; }

    Viewport& viewport() { return vp_; }
    const Viewport& viewport() const { return vp_; }

    VisualNodeCache& cache() { return cache_; }

    // Convenience: direct access to nodes/wires
    std::vector<Node>& nodes() { return bp_->nodes; }
    const std::vector<Node>& nodes() const { return bp_->nodes; }
    std::vector<Wire>& wires() { return bp_->wires; }
    const std::vector<Wire>& wires() const { return bp_->wires; }
    size_t nodeCount() const { return bp_->nodes.size(); }
    size_t wireCount() const { return bp_->wires.size(); }
    float gridStep() const { return bp_->grid_step; }
    const Node* findNode(const char* id) const { return bp_->find_node(id); }
    Node* findNode(const char* id) { return bp_->find_node(id); }

    // ---- Hit testing ----

    HitResult hitTest(Pt world_pos) {
        return hit_test(*bp_, cache_, world_pos, vp_, group_id_);
    }

    HitResult hitTestPorts(Pt world_pos) {
        return hit_test_ports(*bp_, cache_, world_pos, group_id_);
    }

    // ---- Rendering ----

    void render(IDrawList& dl, Pt canvas_min, Pt canvas_max,
                const std::vector<size_t>* selected_nodes = nullptr,
                std::optional<size_t> selected_wire = std::nullopt,
                const an24::Simulator<an24::JIT_Solver>* sim = nullptr)
    {
        renderer_.render(*bp_, dl, vp_, canvas_min, canvas_max, cache_,
                         selected_nodes, selected_wire, sim, group_id_);
    }

    TooltipInfo detectTooltip(Pt world_pos, const an24::Simulator<an24::JIT_Solver>& sim, Pt canvas_min) {
        return renderer_.detectTooltip(*bp_, vp_, canvas_min, cache_, world_pos, sim, group_id_);
    }

    BlueprintRenderer& renderer() { return renderer_; }

    // ---- Scene mutations ----

    size_t addNode(Node node) {
        assert(bp_);
        cache_.getOrCreate(node, bp_->wires);
        return bp_->add_node(std::move(node));
    }

    void removeNode(size_t index) {
        if (index >= bp_->nodes.size()) return;
        // Delegate to removeNodes for consistent recursive cleanup
        removeNodes({index});
    }

    [[nodiscard]] bool addWire(Wire wire) {
        assert(bp_);
        bool ok = bp_->add_wire_validated(std::move(wire));
        if (ok) cache_.onWireAdded(bp_->wires.back(), bp_->nodes);
        return ok;
    }

    void removeWire(size_t index) {
        if (index >= bp_->wires.size()) return;
        Wire copy = bp_->wires[index];
        bp_->wires.erase(bp_->wires.begin() + static_cast<long>(index));
        bp_->wire_index_.erase(WireKey(copy));
        cache_.onWireDeleted(copy, bp_->nodes);
    }

    /// Remove multiple nodes by index (indices must be sorted descending).
    /// Connected wires and collapsed_groups are cleaned automatically.
    /// If any deleted node is a sub-blueprint (expandable), its internal
    /// nodes, wires, and CollapsedGroup entries are recursively removed.
    void removeNodes(const std::vector<size_t>& sorted_desc_indices) {
        // Collect initial set of deleted IDs
        std::unordered_set<std::string> deleted_ids;
        std::unordered_set<std::string> deleted_group_ids;
        for (size_t idx : sorted_desc_indices) {
            if (idx < bp_->nodes.size()) {
                const auto& node = bp_->nodes[idx];
                deleted_ids.insert(node.id);
                // If this is a sub-blueprint, recursively collect its internals
                if (node.expandable) {
                    bp_->collect_group_internals(node.id, deleted_ids, deleted_group_ids);
                }
            }
        }

        // Erase nodes (iterate reverse to preserve indices)
        // We must collect indices for ALL nodes to delete (original + internals)
        for (int i = static_cast<int>(bp_->nodes.size()) - 1; i >= 0; --i) {
            if (deleted_ids.count(bp_->nodes[static_cast<size_t>(i)].id)) {
                bp_->nodes.erase(bp_->nodes.begin() + i);
            }
        }

        // Remove wires connected to any deleted node
        bp_->wires.erase(
            std::remove_if(bp_->wires.begin(), bp_->wires.end(),
                [&deleted_ids](const Wire& w) {
                    return deleted_ids.count(w.start.node_id) || deleted_ids.count(w.end.node_id);
                }),
            bp_->wires.end());

        // Remove CollapsedGroup entries for deleted groups
        bp_->collapsed_groups.erase(
            std::remove_if(bp_->collapsed_groups.begin(), bp_->collapsed_groups.end(),
                [&deleted_ids, &deleted_group_ids](const CollapsedGroup& g) {
                    return deleted_group_ids.count(g.id) || deleted_ids.count(g.id);
                }),
            bp_->collapsed_groups.end());

        // Clean remaining collapsed_groups.internal_node_ids
        for (auto& g : bp_->collapsed_groups) {
            g.internal_node_ids.erase(
                std::remove_if(g.internal_node_ids.begin(), g.internal_node_ids.end(),
                    [&deleted_ids](const std::string& id) { return deleted_ids.count(id); }),
                g.internal_node_ids.end());
        }
        bp_->rebuild_wire_index();
        cache_.clear();
    }

    /// Reconnect one end of a wire to a new port. Clears routing points.
    void reconnectWire(size_t wire_idx, bool reconnect_start, WireEnd new_end) {
        if (wire_idx >= bp_->wires.size()) return;
        auto& wire = bp_->wires[wire_idx];
        bp_->wire_index_.erase(WireKey(wire));
        if (reconnect_start)
            wire.start = new_end;
        else
            wire.end = new_end;
        wire.routing_points.clear();
        bp_->wire_index_.insert(WireKey(wire));
        cache_.clear();
    }

    /// Swap port connections for two wires attached to the same bus.
    /// Preserves existing routing points — only the endpoint (resolved every
    /// frame via resolveWirePort) moves to the new port slot.
    bool swapWirePortsOnBus(const std::string& bus_node_id,
                           const std::string& wire_id_a,
                           const std::string& wire_id_b) {
        // Find wire indices in bp_->wires (authoritative storage)
        size_t idx_a = SIZE_MAX, idx_b = SIZE_MAX;
        for (size_t i = 0; i < bp_->wires.size(); ++i) {
            if (bp_->wires[i].id == wire_id_a) idx_a = i;
            if (bp_->wires[i].id == wire_id_b) idx_b = i;
        }
        if (idx_a == SIZE_MAX || idx_b == SIZE_MAX) return false;

        // Swap visual port ordering on the bus node.
        auto* bus_vis = cache_.get(bus_node_id);
        if (!bus_vis) return false;
        if (!bus_vis->handlePortSwap(wire_id_a, wire_id_b))
            return false;

        // Swap in blueprint so the order persists across save/load.
        std::swap(bp_->wires[idx_a], bp_->wires[idx_b]);
        return true;
    }

    /// Move a node to new_pos, updating both the data and the visual.
    void moveNode(size_t index, Pt new_pos) {
        if (index >= bp_->nodes.size()) return;
        bp_->nodes[index].pos = new_pos;
        auto* vis = cache_.getOrCreate(bp_->nodes[index], bp_->wires);
        if (vis) vis->setPosition(new_pos);
    }

    /// Sync grid step between viewport and blueprint.
    void gridStepUp() {
        vp_.grid_step_up();
        bp_->grid_step = vp_.grid_step;
    }
    void gridStepDown() {
        vp_.grid_step_down();
        bp_->grid_step = vp_.grid_step;
    }

    /// Allocate a unique wire ID string.
    std::string nextWireId() {
        return "wire_" + std::to_string(bp_->next_wire_id++);
    }

    // ---- Utility ----

    Pt portPosition(const Node& node, const char* port_name,
                    const char* wire_id = nullptr) {
        return editor_math::get_port_position(node, port_name,
                                               bp_->wires, wire_id, cache_);
    }

    VisualNode* visual(const Node& node) {
        return cache_.getOrCreate(node, bp_->wires);
    }

    VisualNode* visual(const std::string& node_id) {
        return cache_.get(node_id);
    }

    VisualNode* getVisualNode(size_t index) {
        return (index < bp_->nodes.size()) ? cache_.getOrCreate(bp_->nodes[index], bp_->wires) : nullptr;
    }

    void clearCache() { cache_.clear(); }

    void reset() {
        *bp_ = Blueprint();
        vp_ = Viewport();
        cache_.clear();
    }

    // ---- Persistence ----

    bool save(const char* path) {
        bp_->pan = vp_.pan;
        bp_->zoom = vp_.zoom;
        bp_->grid_step = vp_.grid_step;
        return save_blueprint_to_file(*bp_, path);
    }

    bool load(const char* path) {
        auto bp = load_blueprint_from_file(path);
        if (!bp.has_value()) return false;
        *bp_ = std::move(*bp);
        bp_->rebuild_wire_index();
        vp_.pan = bp_->pan;
        vp_.zoom = bp_->zoom;
        vp_.grid_step = bp_->grid_step;
        vp_.clamp_zoom();
        cache_.clear();
        return true;
    }

private:
    Blueprint* bp_;
    Viewport vp_;
    VisualNodeCache cache_;
    BlueprintRenderer renderer_;
    std::string group_id_;
};

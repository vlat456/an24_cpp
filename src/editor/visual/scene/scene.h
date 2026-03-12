#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual/node/visual_node_cache.h"
#include "visual/hittest.h"
#include "visual/renderer/blueprint_renderer.h"
#include "visual/trigonometry.h"
#include "visual/spatial_grid.h"
#include <cassert>
#include <string>
#include <unordered_set>

/// Scene graph: references shared blueprint, owns visual cache and viewport.
/// Single authority for hit testing, rendering, and scene mutations.
/// Each scene renders a specific nesting level identified by group_id.
class VisualScene {
private:
    Blueprint* bp_;
    Viewport vp_;
    VisualNodeCache cache_;
    BlueprintRenderer renderer_;
    std::string group_id_;
    editor_spatial::SpatialGrid spatial_grid_;
    bool spatial_grid_dirty_ = true;

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
    bool ownsWire(const Wire& w) const;

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

    HitResult hitTest(Pt world_pos);
    HitResult hitTestPorts(Pt world_pos);

    /// Call after any structural mutation: node add/remove/move, wire add/remove.
    void invalidateSpatialGrid() { spatial_grid_dirty_ = true; }
    /// Rebuild only when dirty. Called at start of hitTest/hitTestPorts.
    void ensureSpatialGrid();

    // ---- Rendering ----

    void render(IDrawList& dl, Pt canvas_min, Pt canvas_max,
                const std::vector<size_t>* selected_nodes = nullptr,
                std::optional<size_t> selected_wire = std::nullopt,
                const Simulator<JIT_Solver>* sim = nullptr,
                std::optional<size_t> hovered_wire = std::nullopt);

    TooltipInfo detectTooltip(Pt world_pos, const Simulator<JIT_Solver>& sim, Pt canvas_min);

    BlueprintRenderer& renderer() { return renderer_; }

    // ---- Scene mutations ----

    size_t addNode(Node node);
    void removeNode(size_t index) { removeNodes({index}); }
    [[nodiscard]] bool addWire(Wire wire);
    void removeWire(size_t index);
    /// Remove multiple nodes by index (indices must be sorted descending).
    /// Connected wires and sub_blueprint_instances are cleaned automatically.
    void removeNodes(const std::vector<size_t>& sorted_desc_indices);
    /// Reconnect one end of a wire to a new port. Clears routing points.
    void reconnectWire(size_t wire_idx, bool reconnect_start, WireEnd new_end);
    /// Swap port connections for two wires attached to the same bus.
    bool swapWirePortsOnBus(const std::string& bus_node_id,
                            const std::string& wire_id_a,
                            const std::string& wire_id_b);
    /// Move a node to new_pos, updating both the data and the visual.
    void moveNode(size_t index, Pt new_pos);

    // ---- Grid step ----

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

    Pt portPosition(const Node& node, const char* port_name, const char* wire_id = nullptr) {
        return editor_math::get_port_position(node, port_name, bp_->wires, wire_id, cache_);
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
    void reset();
};

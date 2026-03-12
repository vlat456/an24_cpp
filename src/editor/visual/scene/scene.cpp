#include "scene.h"
#include <algorithm>

// ============================ Hit Testing ============================

HitResult VisualScene::hitTest(Pt world_pos) {
    ensureSpatialGrid();
    return hit_test(*bp_, cache_, world_pos, group_id_, spatial_grid_);
}

HitResult VisualScene::hitTestPorts(Pt world_pos) {
    ensureSpatialGrid();
    return hit_test_ports(*bp_, cache_, world_pos, group_id_, spatial_grid_);
}

void VisualScene::ensureSpatialGrid() {
    if (!spatial_grid_dirty_) return;
    spatial_grid_.rebuild(*bp_, cache_, group_id_);
    spatial_grid_dirty_ = false;
}

// ============================ Rendering ============================

void VisualScene::render(IDrawList& dl, Pt canvas_min, Pt canvas_max,
                         const std::vector<size_t>* selected_nodes,
                         std::optional<size_t> selected_wire,
                         const Simulator<JIT_Solver>* sim,
                         std::optional<size_t> hovered_wire) {
    renderer_.render(*bp_, dl, vp_, canvas_min, canvas_max, cache_,
                     selected_nodes, selected_wire, sim, hovered_wire, group_id_);
}

TooltipInfo VisualScene::detectTooltip(Pt world_pos, const Simulator<JIT_Solver>& sim,
                                       Pt canvas_min) {
    ensureSpatialGrid();
    return renderer_.detectTooltip(*bp_, vp_, canvas_min, cache_, world_pos, sim,
                                   spatial_grid_, group_id_);
}

// ============================ Ownership ============================

bool VisualScene::ownsWire(const Wire& w) const {
    const Node* sn = bp_->find_node(w.start.node_id.c_str());
    const Node* en = bp_->find_node(w.end.node_id.c_str());
    return sn && en && sn->group_id == group_id_ && en->group_id == group_id_;
}

size_t VisualScene::addNode(Node node) {
    assert(bp_);
    cache_.getOrCreate(node, bp_->wires);
    size_t idx = bp_->add_node(std::move(node));
    invalidateSpatialGrid();
    return idx;
}

bool VisualScene::addWire(Wire wire) {
    assert(bp_);
    bool ok = bp_->add_wire_validated(std::move(wire));
    if (ok) {
        cache_.onWireAdded(bp_->wires.back(), bp_->nodes);
        invalidateSpatialGrid();
    }
    return ok;
}

void VisualScene::removeWire(size_t index) {
    if (index >= bp_->wires.size()) return;
    Wire copy = bp_->wires[index];
    bp_->wires.erase(bp_->wires.begin() + static_cast<long>(index));
    bp_->wire_index_.erase(WireKey(copy));
    cache_.onWireDeleted(copy, bp_->nodes);
    invalidateSpatialGrid();
}

void VisualScene::removeNodes(const std::vector<size_t>& sorted_desc_indices) {
    std::unordered_set<std::string> deleted_ids;
    std::unordered_set<std::string> deleted_group_ids;
    for (size_t idx : sorted_desc_indices) {
        if (idx < bp_->nodes.size()) {
            const auto& node = bp_->nodes[idx];
            deleted_ids.insert(node.id);
            if (node.expandable) {
                bp_->collect_group_internals(node.id, deleted_ids, deleted_group_ids);
            }
        }
    }

    for (int i = static_cast<int>(bp_->nodes.size()) - 1; i >= 0; --i) {
        if (deleted_ids.count(bp_->nodes[static_cast<size_t>(i)].id)) {
            bp_->nodes.erase(bp_->nodes.begin() + i);
        }
    }

    bp_->wires.erase(
        std::remove_if(bp_->wires.begin(), bp_->wires.end(),
            [&deleted_ids](const Wire& w) {
                return deleted_ids.count(w.start.node_id) || deleted_ids.count(w.end.node_id);
            }),
        bp_->wires.end());

    bp_->sub_blueprint_instances.erase(
        std::remove_if(bp_->sub_blueprint_instances.begin(), bp_->sub_blueprint_instances.end(),
            [&deleted_ids, &deleted_group_ids](const SubBlueprintInstance& g) {
                return deleted_group_ids.count(g.id) || deleted_ids.count(g.id);
            }),
        bp_->sub_blueprint_instances.end());

    for (auto& g : bp_->sub_blueprint_instances) {
        g.internal_node_ids.erase(
            std::remove_if(g.internal_node_ids.begin(), g.internal_node_ids.end(),
                [&deleted_ids](const std::string& id) { return deleted_ids.count(id); }),
            g.internal_node_ids.end());
    }
    bp_->rebuild_wire_index();
    cache_.clear();
    invalidateSpatialGrid();
}

void VisualScene::reconnectWire(size_t wire_idx, bool reconnect_start, WireEnd new_end) {
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
    invalidateSpatialGrid();
}

bool VisualScene::swapWirePortsOnBus(const std::string& bus_node_id,
                                     const std::string& wire_id_a,
                                     const std::string& wire_id_b) {
    size_t idx_a = SIZE_MAX, idx_b = SIZE_MAX;
    for (size_t i = 0; i < bp_->wires.size(); ++i) {
        if (bp_->wires[i].id == wire_id_a) idx_a = i;
        if (bp_->wires[i].id == wire_id_b) idx_b = i;
    }
    if (idx_a == SIZE_MAX || idx_b == SIZE_MAX) return false;

    auto* bus_vis = cache_.get(bus_node_id);
    if (!bus_vis) return false;
    if (!bus_vis->handlePortSwap(wire_id_a, wire_id_b))
        return false;

    std::swap(bp_->wires[idx_a], bp_->wires[idx_b]);
    invalidateSpatialGrid();
    return true;
}

void VisualScene::moveNode(size_t index, Pt new_pos) {
    if (index >= bp_->nodes.size()) return;
    bp_->nodes[index].pos = new_pos;
    auto* vis = cache_.getOrCreate(bp_->nodes[index], bp_->wires);
    if (vis) vis->setPosition(new_pos);
}

void VisualScene::reset() {
    *bp_ = Blueprint();
    vp_ = Viewport();
    cache_.clear();
    invalidateSpatialGrid();
}

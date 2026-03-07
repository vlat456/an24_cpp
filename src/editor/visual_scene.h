#pragma once

#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual_node.h"
#include "hittest.h"
#include "render.h"
#include "trigonometry.h"

/// Scene graph: owns blueprint data, visual cache, and viewport.
/// Single authority for hit testing, rendering, and scene mutations.
/// EditorApp acts as controller — owns VisualScene + Interaction + Simulation.
class VisualScene {
public:
    VisualScene() = default;

    // ---- Data access (for read + gradual migration) ----

    Blueprint& blueprint() { return bp_; }
    const Blueprint& blueprint() const { return bp_; }

    Viewport& viewport() { return vp_; }
    const Viewport& viewport() const { return vp_; }

    VisualNodeCache& cache() { return cache_; }

    // ---- Hit testing ----

    HitResult hitTest(Pt world_pos) {
        return hit_test(bp_, cache_, world_pos, vp_);
    }

    HitResult hitTestPorts(Pt world_pos) {
        return hit_test_ports(bp_, cache_, world_pos);
    }

    // ---- Rendering ----

    void render(IDrawList* dl, Pt canvas_min, Pt canvas_max,
                const std::vector<size_t>* selected_nodes = nullptr,
                std::optional<size_t> selected_wire = std::nullopt,
                const an24::Simulator<an24::JIT_Solver>* simulation = nullptr,
                const Pt* hover_world_pos = nullptr,
                TooltipInfo* out_tooltip = nullptr)
    {
        render_blueprint(bp_, dl, vp_, canvas_min, canvas_max, cache_,
                         selected_nodes, selected_wire, simulation,
                         hover_world_pos, out_tooltip);
    }

    // ---- Scene mutations ----

    size_t addNode(Node node) {
        cache_.getOrCreate(node, bp_.wires);
        return bp_.add_node(std::move(node));
    }

    void removeNode(size_t index) {
        if (index >= bp_.nodes.size()) return;
        const auto& nid = bp_.nodes[index].id;
        // Remove connected wires (reverse to keep indices valid)
        for (int i = static_cast<int>(bp_.wires.size()) - 1; i >= 0; --i) {
            const auto& w = bp_.wires[static_cast<size_t>(i)];
            if (w.start.node_id == nid || w.end.node_id == nid)
                bp_.wires.erase(bp_.wires.begin() + i);
        }
        bp_.nodes.erase(bp_.nodes.begin() + static_cast<long>(index));
        cache_.clear();
    }

    bool addWire(Wire wire) {
        bool ok = bp_.add_wire_validated(std::move(wire));
        if (ok) cache_.onWireAdded(bp_.wires.back(), bp_.nodes);
        return ok;
    }

    void removeWire(size_t index) {
        if (index >= bp_.wires.size()) return;
        Wire copy = bp_.wires[index];
        bp_.wires.erase(bp_.wires.begin() + static_cast<long>(index));
        cache_.onWireDeleted(copy, bp_.nodes);
    }

    // ---- Utility ----

    Pt portPosition(const Node& node, const char* port_name,
                    const char* wire_id = nullptr) {
        return editor_math::get_port_position(node, port_name,
                                               bp_.wires, wire_id, cache_);
    }

    VisualNode* visual(const Node& node) {
        return cache_.getOrCreate(node, bp_.wires);
    }

    VisualNode* visual(const std::string& node_id) {
        return cache_.get(node_id);
    }

    void clearCache() { cache_.clear(); }

    void reset() {
        bp_ = Blueprint();
        vp_ = Viewport();
        cache_.clear();
    }

private:
    Blueprint bp_;
    Viewport vp_;
    VisualNodeCache cache_;
};

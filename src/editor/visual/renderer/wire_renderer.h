#pragma once

#include "visual/renderer/draw_list.h"
#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual/node/node.h"
#include "jit_solver/simulator.h"
#include <optional>
#include <vector>

/// Renders all wires in a blueprint: polylines with crossing gaps,
/// routing points, jump arcs, and energized-wire highlighting.
class WireRenderer {
public:
    void render(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                Pt canvas_min, VisualNodeCache& cache,
                const an24::Simulator<an24::JIT_Solver>* sim,
                std::optional<size_t> selected_wire,
                std::optional<size_t> hovered_wire = std::nullopt,
                const std::string& group_id = "");

private:
    std::vector<std::vector<Pt>> polylines_;
};

#pragma once

#include "visual/renderer/draw_list.h"
#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual/node/node.h"
#include <vector>

/// Renders all visible nodes by delegating to VisualNodeCache → VisualNode::render().
class NodeRenderer {
public:
    void render(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                Pt canvas_min, VisualNodeCache& cache,
                const std::vector<size_t>* selected_nodes);
};

#pragma once

#include "visual/renderer/draw_list.h"
#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual/node/node.h"
#include <vector>

/// Renders nodes belonging to a specific group by delegating to VisualNodeCache → VisualNode::render().
/// Split into groups/non-groups for correct z-ordering (grid → groups → wires → nodes).
class NodeRenderer {
public:
    /// Render only group nodes (isGroup() == true), drawn below wires.
    void renderGroups(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                      Pt canvas_min, VisualNodeCache& cache,
                      const std::vector<size_t>* selected_nodes,
                      const std::string& group_id = "");

    /// Render only non-group nodes (isGroup() == false), drawn above wires.
    void renderNodes(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                     Pt canvas_min, VisualNodeCache& cache,
                     const std::vector<size_t>* selected_nodes,
                     const std::string& group_id = "");

    /// Render resize handles for selected resizable nodes.
    void renderResizeHandles(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                             Pt canvas_min, VisualNodeCache& cache,
                             const std::vector<size_t>* selected_nodes,
                             const std::string& group_id = "");

private:
    void renderFiltered(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                        Pt canvas_min, VisualNodeCache& cache,
                        const std::vector<size_t>* selected_nodes,
                        const std::string& group_id, bool groups_only);
};

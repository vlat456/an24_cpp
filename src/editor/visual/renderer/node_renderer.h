#pragma once

#include "visual/renderer/draw_list.h"
#include "data/blueprint.h"
#include "viewport/viewport.h"
#include "visual/node/visual_node_cache.h"
#include <vector>

/// Renders nodes belonging to a specific group by delegating to VisualNodeCache → VisualNode::render().
/// Split into layers for correct z-ordering (grid → groups → texts → wires → nodes).
class NodeRenderer {
public:
    /// Render only group-layer nodes, drawn below texts and wires.
    void renderGroups(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                      Pt canvas_min, VisualNodeCache& cache,
                      const std::vector<size_t>* selected_nodes,
                      const std::string& group_id = "");

    /// Render only text-layer nodes, drawn above groups but below wires.
    void renderTexts(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                     Pt canvas_min, VisualNodeCache& cache,
                     const std::vector<size_t>* selected_nodes,
                     const std::string& group_id = "");

    /// Render only normal nodes, drawn above wires.
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
                        const std::string& group_id, RenderLayer layer);
};

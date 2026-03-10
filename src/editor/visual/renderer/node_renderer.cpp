#include "visual/renderer/node_renderer.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/handle_renderer.h"
#include "layout_constants.h"

void NodeRenderer::renderGroups(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                                Pt canvas_min, VisualNodeCache& cache,
                                const std::vector<size_t>* selected_nodes,
                                const std::string& group_id) {
    renderFiltered(bp, dl, vp, canvas_min, cache, selected_nodes, group_id, true);
}

void NodeRenderer::renderNodes(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                               Pt canvas_min, VisualNodeCache& cache,
                               const std::vector<size_t>* selected_nodes,
                               const std::string& group_id) {
    renderFiltered(bp, dl, vp, canvas_min, cache, selected_nodes, group_id, false);
}

void NodeRenderer::renderFiltered(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                                  Pt canvas_min, VisualNodeCache& cache,
                                  const std::vector<size_t>* selected_nodes,
                                  const std::string& group_id, bool groups_only) {
    size_t node_idx = 0;
    for (const auto& n : bp.nodes) {
        if (n.group_id != group_id) {
            node_idx++;
            continue;
        }

        auto* visual = cache.getOrCreate(n, bp.wires);

        if (visual->isGroup() != groups_only) {
            node_idx++;
            continue;
        }

        bool is_selected = false;
        if (selected_nodes) {
            for (size_t idx : *selected_nodes) {
                if (idx == node_idx) { is_selected = true; break; }
            }
        }

        visual->render(&dl, vp, canvas_min, is_selected);
        node_idx++;
    }
}

void NodeRenderer::renderResizeHandles(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                                       Pt canvas_min, VisualNodeCache& cache,
                                       const std::vector<size_t>* selected_nodes,
                                       const std::string& group_id) {
    if (!selected_nodes) return;

    float handle_size = editor_constants::RESIZE_HANDLE_SIZE * vp.zoom;

    for (size_t idx : *selected_nodes) {
        if (idx >= bp.nodes.size()) continue;
        const auto& n = bp.nodes[idx];
        if (n.group_id != group_id) continue;

        auto* visual = cache.getOrCreate(n, bp.wires);
        if (!visual->isResizable()) continue;

        Pt pos = visual->getPosition();
        Pt sz = visual->getSize();

        Pt corners[] = {
            pos,
            Pt(pos.x + sz.x, pos.y),
            Pt(pos.x, pos.y + sz.y),
            Pt(pos.x + sz.x, pos.y + sz.y),
        };

        for (const auto& corner : corners) {
            Pt screen = vp.world_to_screen(corner, canvas_min);
            handle_renderer::draw_handle(dl, screen, handle_size / 2,
                                         render_theme::COLOR_RESIZE_HANDLE);
        }
    }
}

#include "visual/renderer/tooltip_detector.h"
#include "visual/port/port.h"
#include "visual/trigonometry.h"
#include "layout_constants.h"
#include <algorithm>
#include <cmath>
#include <cstdio>


TooltipInfo TooltipDetector::detect(const Blueprint& bp, const Viewport& vp,
                                    Pt canvas_min, VisualNodeCache& cache,
                                    Pt world_pos,
                                    const Simulator<JIT_Solver>& sim,
                                    const editor_spatial::SpatialGrid& grid,
                                    const std::string& group_id) const {
    constexpr float TOOLTIP_RADIUS = 8.0f;
    TooltipInfo result;

    // Check ports (via spatial grid)
    {
        std::vector<size_t> candidates;
        candidates.reserve(8);
        grid.query_nodes(world_pos, TOOLTIP_RADIUS, candidates);

        for (size_t ni : candidates) {
            if (ni >= bp.nodes.size()) continue;
            const auto& n = bp.nodes[ni];
            if (n.group_id != group_id) continue;
            auto* vis = cache.getOrCreate(n, bp.wires);
            for (size_t pi = 0; pi < vis->getPortCount(); pi++) {
                auto* port = vis->getPort(pi);
                if (!port) continue;
                Pt port_wpos = port->worldPosition();
                float dx = world_pos.x - port_wpos.x;
                float dy = world_pos.y - port_wpos.y;
                if (dx * dx + dy * dy <= TOOLTIP_RADIUS * TOOLTIP_RADIUS) {
                    std::string logical_port = port->logicalName();
                    float val = sim.get_port_value(n.id, logical_port);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.3f", val);
                    result.active = true;
                    result.screen_pos = vp.world_to_screen(port_wpos, canvas_min);
                    result.label = n.id + "." + logical_port;
                    result.text = buf;
                    return result;
                }
            }
        }
    }

    // Check wire segments (via spatial grid)
    {
        std::vector<size_t> candidates;
        candidates.reserve(8);
        grid.query_wires(world_pos, TOOLTIP_RADIUS, candidates);

        for (size_t wi : candidates) {
            if (wi >= bp.wires.size()) continue;
            const auto& w = bp.wires[wi];
            const Node* sn = bp.find_node(w.start.node_id.c_str());
            const Node* en = bp.find_node(w.end.node_id.c_str());
            if (!sn || !en || sn->group_id != group_id || en->group_id != group_id) continue;

            Pt start_pos = editor_math::get_port_position(
                *sn, w.start.port_name.c_str(), bp.wires, w.id.c_str(), cache);
            Pt end_pos = editor_math::get_port_position(
                *en, w.end.port_name.c_str(), bp.wires, w.id.c_str(), cache);

            // Walk segments: start → rp[0] → rp[1] → ... → end
            Pt prev = start_pos;
            auto check_segment = [&](Pt a, Pt b) -> bool {
                float seg_dx = b.x - a.x, seg_dy = b.y - a.y;
                float seg_len_sq = seg_dx * seg_dx + seg_dy * seg_dy;
                if (seg_len_sq < 1e-6f) return false;
                float t = ((world_pos.x - a.x) * seg_dx + (world_pos.y - a.y) * seg_dy) / seg_len_sq;
                t = std::max(0.0f, std::min(1.0f, t));
                Pt proj(a.x + t * seg_dx, a.y + t * seg_dy);
                float dx = world_pos.x - proj.x, dy = world_pos.y - proj.y;
                if (dx * dx + dy * dy > TOOLTIP_RADIUS * TOOLTIP_RADIUS) return false;

                std::string port = w.start.node_id + "." + w.start.port_name;
                float val = sim.get_wire_voltage(port);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.3f", val);
                result.active = true;
                result.screen_pos = vp.world_to_screen(proj, canvas_min);
                result.label = port;
                result.text = buf;
                return true;
            };

            for (const auto& rp : w.routing_points) {
                if (check_segment(prev, rp)) return result;
                prev = rp;
            }
            if (check_segment(prev, end_pos)) return result;
        }
    }

    return result;
}

void TooltipDetector::renderTooltip(IDrawList& dl, const TooltipInfo& tooltip) {
    if (!tooltip.active) return;

    constexpr float FONT_SIZE = 14.0f;
    constexpr float PAD = 4.0f;
    constexpr uint32_t BG_COLOR = 0xCC1A1A1A;
    constexpr uint32_t LABEL_COLOR = 0xFFAAAAAA;
    constexpr uint32_t VALUE_COLOR = 0xFFFFFFFF;

    std::string full = tooltip.label + ": " + tooltip.text;
    Pt text_size = dl.calc_text_size(full.c_str(), FONT_SIZE);

    Pt bg_min(tooltip.screen_pos.x, tooltip.screen_pos.y - text_size.y - PAD * 2);
    Pt bg_max(tooltip.screen_pos.x + text_size.x + PAD * 2, tooltip.screen_pos.y);

    dl.add_rect_filled(bg_min, bg_max, BG_COLOR);
    dl.add_text(Pt(bg_min.x + PAD, bg_min.y + PAD),
                (tooltip.label + ": ").c_str(), LABEL_COLOR, FONT_SIZE);

    Pt label_size = dl.calc_text_size((tooltip.label + ": ").c_str(), FONT_SIZE);
    dl.add_text(Pt(bg_min.x + PAD + label_size.x, bg_min.y + PAD),
                tooltip.text.c_str(), VALUE_COLOR, FONT_SIZE);
}

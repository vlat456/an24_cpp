#include "visual/renderer/tooltip_detector.h"
#include "visual/port/port.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

TooltipInfo TooltipDetector::detect(const Blueprint& bp, const Viewport& vp,
                                    Pt canvas_min, VisualNodeCache& cache,
                                    Pt world_pos,
                                    const an24::Simulator<an24::JIT_Solver>& sim,
                                    const std::vector<std::vector<Pt>>& polylines,
                                    const std::string& group_id) const {
    constexpr float PORT_RADIUS = 8.0f;
    TooltipInfo result;

    // Check ports
    for (const auto& n : bp.nodes) {
        if (n.group_id != group_id) continue;
        auto* vis = cache.getOrCreate(n, bp.wires);
        for (size_t pi = 0; pi < vis->getPortCount(); pi++) {
            auto* port = vis->getPort(pi);
            if (!port) continue;
            Pt port_wpos = port->worldPosition();
            float dx = world_pos.x - port_wpos.x;
            float dy = world_pos.y - port_wpos.y;
            if (dx * dx + dy * dy <= PORT_RADIUS * PORT_RADIUS) {
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

    // Check wire segments
    for (size_t wi = 0; wi < bp.wires.size() && wi < polylines.size(); wi++) {
        const auto& poly = polylines[wi];
        for (size_t i = 0; i + 1 < poly.size(); i++) {
            Pt a = poly[i], b = poly[i + 1];
            float seg_len_sq = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
            if (seg_len_sq < 1e-6f) continue;
            float t = ((world_pos.x - a.x) * (b.x - a.x) + (world_pos.y - a.y) * (b.y - a.y)) / seg_len_sq;
            t = std::max(0.0f, std::min(1.0f, t));
            Pt proj(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
            float dx = world_pos.x - proj.x;
            float dy = world_pos.y - proj.y;
            float dist_sq = dx * dx + dy * dy;

            if (dist_sq <= PORT_RADIUS * PORT_RADIUS) {
                const auto& w = bp.wires[wi];
                std::string port = w.start.node_id + "." + w.start.port_name;
                float val = sim.get_wire_voltage(port);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.3f", val);
                result.active = true;
                result.screen_pos = vp.world_to_screen(proj, canvas_min);
                result.label = port;
                result.text = buf;
                return result;
            }
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

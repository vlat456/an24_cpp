#include "visual/renderer/wire_renderer.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/renderer/wire/polyline_builder.h"
#include "visual/renderer/wire/polyline_draw.h"
#include "visual/renderer/wire/arc_draw.h"
#include "layout_constants.h"
#include "router/crossings.h"
#include <unordered_map>

void WireRenderer::render(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                          Pt canvas_min, VisualNodeCache& cache,
                          const Simulator<JIT_Solver>* sim,
                          std::optional<size_t> selected_wire,
                          std::optional<size_t> hovered_wire,
                          const std::string& group_id) {
    using namespace render_theme;

    polylines_ = polyline_builder::build_wire_polylines(bp, group_id, cache);

    std::vector<std::vector<WireCrossing>> all_crossings;
    all_crossings.resize(bp.wires.size());
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        all_crossings[wire_idx] = find_wire_crossings(wire_idx, polylines_);
    }

    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& w = bp.wires[wire_idx];
        const auto& poly = polylines_[wire_idx];
        if (poly.size() < 2) continue;

        bool is_selected = selected_wire.has_value() && *selected_wire == wire_idx;
        bool is_hovered = hovered_wire.has_value() && *hovered_wire == wire_idx;
        uint32_t wire_color;
        if (is_selected) {
            wire_color = COLOR_WIRE;
        } else if (is_hovered) {
            wire_color = COLOR_WIRE_HOVER;
        } else {
            wire_color = COLOR_WIRE_UNSEL;
        }

        if (sim && sim->is_running() && !w.start.node_id.empty()) {
            std::string start_port = w.start.node_id + "." + w.start.port_name;
            if (sim->wire_is_energized(start_port, 0.5f)) {
                wire_color = COLOR_WIRE_CURRENT;
            }
        }

        const auto& crossings = all_crossings[wire_idx];

        if (is_selected || is_hovered) {
            for (const auto& rp : w.routing_points) {
                Pt screen_rp = vp.world_to_screen(rp, canvas_min);
                float r = editor_constants::ROUTING_POINT_RADIUS * vp.zoom;
                handle_renderer::draw_handle(dl, screen_rp, r, COLOR_ROUTING_POINT);
            }
        }

        auto segs_crossings = polyline_draw::classify_crossings_by_segment(crossings, poly);
        polyline_draw::draw_polyline_with_gaps(dl, poly, segs_crossings, vp, canvas_min,
                                               wire_color, ARC_RADIUS_WORLD);

        arc_draw::draw_all_arcs(dl, crossings, vp, canvas_min, wire_color);
    }
}

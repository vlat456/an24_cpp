#include "visual/renderer/wire_renderer.h"
#include "visual/renderer/render_theme.h"
#include "layout_constants.h"
#include "visual/trigonometry.h"
#include "router/crossings.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

void WireRenderer::render(const Blueprint& bp, IDrawList& dl, const Viewport& vp,
                          Pt canvas_min, VisualNodeCache& cache,
                          const an24::Simulator<an24::JIT_Solver>* sim,
                          std::optional<size_t> selected_wire,
                          const std::string& group_id) {
    using namespace render_theme;

    // [PERF-s1t2] Build node lookup map — was O(n) per wire, now O(1)
    std::unordered_map<std::string, const Node*> node_map;
    node_map.reserve(bp.nodes.size());
    for (const auto& n : bp.nodes)
        node_map[n.id] = &n;

    // Build polylines for all wires (for crossing detection + tooltip later)
    polylines_.clear();
    polylines_.reserve(bp.wires.size());

    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& w = bp.wires[wire_idx];

        auto it_s = node_map.find(w.start.node_id);
        auto it_e = node_map.find(w.end.node_id);
        const Node* start_node = (it_s != node_map.end()) ? it_s->second : nullptr;
        const Node* end_node   = (it_e != node_map.end()) ? it_e->second : nullptr;

        if (!start_node || !end_node) {
            polylines_.push_back({});
            continue;
        }

        // Skip wires whose endpoints are not in this group
        if (start_node->group_id != group_id || end_node->group_id != group_id) {
            polylines_.push_back({});
            continue;
        }

        Pt start_pos = editor_math::get_port_position(*start_node, w.start.port_name.c_str(),
                                                       bp.wires, w.id.c_str(), cache);
        Pt end_pos = editor_math::get_port_position(*end_node, w.end.port_name.c_str(),
                                                     bp.wires, w.id.c_str(), cache);

        std::vector<Pt> poly;
        poly.push_back(start_pos);
        poly.insert(poly.end(), w.routing_points.begin(), w.routing_points.end());
        poly.push_back(end_pos);

        polylines_.push_back(std::move(poly));
    }

    // Find all crossings (only higher-index wire draws arc)
    std::vector<std::vector<WireCrossing>> all_crossings;
    all_crossings.resize(bp.wires.size());
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        all_crossings[wire_idx] = find_wire_crossings(wire_idx, polylines_);
    }

    // Render wires (before nodes so wires appear underneath)
    for (size_t wire_idx = 0; wire_idx < bp.wires.size(); wire_idx++) {
        const auto& w = bp.wires[wire_idx];
        const auto& poly = polylines_[wire_idx];
        if (poly.size() < 2) continue;

        bool is_selected = selected_wire.has_value() && *selected_wire == wire_idx;
        uint32_t wire_color = is_selected ? COLOR_WIRE : COLOR_WIRE_UNSEL;

        // Energized wire highlighting
        if (sim && sim->is_running() && !w.start.node_id.empty()) {
            std::string start_port = w.start.node_id + "." + w.start.port_name;
            if (sim->wire_is_energized(start_port, 0.5f)) {
                wire_color = COLOR_WIRE_CURRENT;
            }
        }

        const auto& crossings = all_crossings[wire_idx];

        // Routing points (draw BEFORE wires so they appear underneath)
        for (const auto& rp : w.routing_points) {
            Pt screen_rp = vp.world_to_screen(rp, canvas_min);
            dl.add_circle_filled(screen_rp, editor_constants::ROUTING_POINT_RADIUS * vp.zoom, COLOR_ROUTING_POINT, 12);
        }

        // Classify crossings by segment
        struct CrossOnSeg {
            size_t seg_idx;
            float t;
            Pt pos;
            SegDir my_seg_dir;
        };
        std::vector<CrossOnSeg> segs_crossings;
        for (const auto& c : crossings) {
            for (size_t i = 0; i + 1 < poly.size(); i++) {
                Pt a = poly[i], b = poly[i + 1];
                float seg_len_sq = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
                if (seg_len_sq < 1e-6f) continue;
                float t = ((c.pos.x - a.x) * (b.x - a.x) + (c.pos.y - a.y) * (b.y - a.y)) / seg_len_sq;
                if (t >= -0.01f && t <= 1.01f) {
                    Pt proj(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
                    float dist = std::sqrt((proj.x - c.pos.x) * (proj.x - c.pos.x) +
                                           (proj.y - c.pos.y) * (proj.y - c.pos.y));
                    if (dist < 1.0f) {
                        segs_crossings.push_back({i, std::max(0.0f, std::min(1.0f, t)), c.pos, c.my_seg_dir});
                        break;
                    }
                }
            }
        }

        // Draw polyline with gaps at crossings
        if (segs_crossings.empty()) {
            std::vector<Pt> screen_pts;
            screen_pts.reserve(poly.size());
            for (const auto& p : poly)
                screen_pts.push_back(vp.world_to_screen(p, canvas_min));
            dl.add_polyline(screen_pts.data(), screen_pts.size(), wire_color, 2.0f);
        } else {
            std::sort(segs_crossings.begin(), segs_crossings.end(),
                      [](const CrossOnSeg& a, const CrossOnSeg& b) {
                          return a.seg_idx < b.seg_idx || (a.seg_idx == b.seg_idx && a.t < b.t);
                      });

            float gap_r = ARC_RADIUS_WORLD;
            std::vector<Pt> current_sub;
            size_t cross_i = 0;

            for (size_t seg = 0; seg + 1 < poly.size(); seg++) {
                Pt a = poly[seg], b = poly[seg + 1];
                float seg_len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));

                std::vector<float> seg_ts;
                while (cross_i < segs_crossings.size() && segs_crossings[cross_i].seg_idx == seg) {
                    seg_ts.push_back(segs_crossings[cross_i].t);
                    cross_i++;
                }

                if (seg_ts.empty()) {
                    if (current_sub.empty())
                        current_sub.push_back(vp.world_to_screen(a, canvas_min));
                    current_sub.push_back(vp.world_to_screen(b, canvas_min));
                } else {
                    float prev_t = 0.0f;
                    if (current_sub.empty())
                        current_sub.push_back(vp.world_to_screen(a, canvas_min));
                    for (float ct : seg_ts) {
                        float gap_t = (seg_len > 1e-3f) ? gap_r / seg_len : 0.5f;
                        float t_before = ct - gap_t;
                        float t_after  = ct + gap_t;

                        if (t_before > prev_t + 0.001f) {
                            Pt p_before(a.x + t_before * (b.x - a.x), a.y + t_before * (b.y - a.y));
                            current_sub.push_back(vp.world_to_screen(p_before, canvas_min));
                        }

                        if (current_sub.size() >= 2)
                            dl.add_polyline(current_sub.data(), current_sub.size(), wire_color, 2.0f);
                        current_sub.clear();

                        if (t_after < 1.0f - 0.001f) {
                            Pt p_after(a.x + t_after * (b.x - a.x), a.y + t_after * (b.y - a.y));
                            current_sub.push_back(vp.world_to_screen(p_after, canvas_min));
                        }
                        prev_t = t_after;
                    }

                    if (prev_t < 1.0f - 0.001f) {
                        if (current_sub.empty()) {
                            float gap_t = (seg_len > 1e-3f) ? gap_r / seg_len : 0.5f;
                            float last_after = seg_ts.back() + gap_t;
                            if (last_after < 1.0f) {
                                Pt p(a.x + last_after * (b.x - a.x), a.y + last_after * (b.y - a.y));
                                current_sub.push_back(vp.world_to_screen(p, canvas_min));
                            }
                        }
                        current_sub.push_back(vp.world_to_screen(b, canvas_min));
                    }
                }
            }

            if (current_sub.size() >= 2)
                dl.add_polyline(current_sub.data(), current_sub.size(), wire_color, 2.0f);
        }

        // Jump arcs at crossings
        for (const auto& crossing : crossings) {
            Pt screen_cross = vp.world_to_screen(crossing.pos, canvas_min);
            float arc_radius = ARC_RADIUS_WORLD * vp.zoom;

            bool arc_vertical = (crossing.my_seg_dir == SegDir::Horiz ||
                                 crossing.my_seg_dir == SegDir::Unknown);

            Pt arc_points[ARC_SEGMENTS + 1];
            for (int i = 0; i <= ARC_SEGMENTS; i++) {
                float angle = 3.14159265f * i / ARC_SEGMENTS;
                if (arc_vertical) {
                    Pt offset(std::cos(angle) * arc_radius, std::sin(angle) * arc_radius);
                    arc_points[i] = Pt(screen_cross.x + offset.x, screen_cross.y - offset.y);
                } else {
                    Pt offset(std::sin(angle) * arc_radius, std::cos(angle) * arc_radius);
                    arc_points[i] = Pt(screen_cross.x + offset.x, screen_cross.y + offset.y);
                }
            }
            dl.add_polyline(arc_points, ARC_SEGMENTS + 1, wire_color, 2.0f);
        }
    }
}

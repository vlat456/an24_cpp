#include "polyline_draw.h"
#include <algorithm>
#include <cmath>

namespace polyline_draw {

std::vector<CrossOnSeg> classify_crossings_by_segment(
    const std::vector<WireCrossing>& crossings,
    const std::vector<Pt>& poly) {
    
    std::vector<CrossOnSeg> result;
    
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
                    result.push_back({i, std::max(0.0f, std::min(1.0f, t)), c.pos, c.my_seg_dir});
                    break;
                }
            }
        }
    }
    
    return result;
}

void draw_polyline_with_gaps(IDrawList& dl, const std::vector<Pt>& poly,
                             const std::vector<CrossOnSeg>& segs_crossings,
                             const Viewport& vp, Pt canvas_min,
                             uint32_t wire_color, float gap_radius) {
    if (segs_crossings.empty()) {
        std::vector<Pt> screen_pts;
        screen_pts.reserve(poly.size());
        for (const auto& p : poly)
            screen_pts.push_back(vp.world_to_screen(p, canvas_min));
        dl.add_polyline(screen_pts.data(), screen_pts.size(), wire_color, 2.0f);
        return;
    }

    std::vector<CrossOnSeg> sorted_crossings = segs_crossings;
    std::sort(sorted_crossings.begin(), sorted_crossings.end(),
              [](const CrossOnSeg& a, const CrossOnSeg& b) {
                  return a.seg_idx < b.seg_idx || (a.seg_idx == b.seg_idx && a.t < b.t);
              });

    std::vector<Pt> current_sub;
    size_t cross_i = 0;

    for (size_t seg = 0; seg + 1 < poly.size(); seg++) {
        Pt a = poly[seg], b = poly[seg + 1];
        float seg_len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));

        std::vector<float> seg_ts;
        while (cross_i < sorted_crossings.size() && sorted_crossings[cross_i].seg_idx == seg) {
            seg_ts.push_back(sorted_crossings[cross_i].t);
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
                float gap_t = (seg_len > 1e-3f) ? gap_radius / seg_len : 0.5f;
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
                    float gap_t = (seg_len > 1e-3f) ? gap_radius / seg_len : 0.5f;
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

}

#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "visual/scene.h"
#include "visual/port/visual_port.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/render_context.h"
#include "router/crossings.h"
#include "ui/core/small_vector.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace visual {

Wire::Wire(const std::string& id,
           const std::string& start_node, const std::string& start_port,
           const std::string& end_node, const std::string& end_port)
    : id_(id)
{
    start_.node_id = start_node;
    start_.port_name = start_port;
    start_.wire_id = id;
    end_.node_id = end_node;
    end_.port_name = end_port;
    end_.wire_id = id;
}

std::optional<Pt> Wire::resolveEndpoint(const WireEndpoint& ep) const {
    if (ep.node_id.empty()) return std::nullopt;
    if (!scene_) return std::nullopt;

    auto* node_widget = scene_->find(ep.node_id);
    if (!node_widget) return std::nullopt;

    auto* port = node_widget->portByName(ep.port_name, ep.wire_id);
    if (!port) return std::nullopt;

    // Port center = port worldPos + (RADIUS, RADIUS)
    Pt pos = port->worldPos();
    return Pt(pos.x + Port::RADIUS, pos.y + Port::RADIUS);
}

const std::vector<Pt>& Wire::polyline() const {
    rebuildGeometry();
    return cached_polyline_;
}

Pt Wire::worldMin() const {
    rebuildGeometry();
    return cached_min_;
}

Pt Wire::worldMax() const {
    rebuildGeometry();
    return cached_max_;
}

void Wire::rebuildGeometry() const {
    // Resolve current endpoint positions
    auto opt_start = resolveEndpoint(start_);
    auto opt_end   = resolveEndpoint(end_);
    Pt cur_start = opt_start.value_or(Pt(0, 0));
    Pt cur_end   = opt_end.value_or(Pt(0, 0));

    // Auto-detect endpoint movement
    if (!dirty_) {
        if (cur_start.x != cached_start_pos_.x || cur_start.y != cached_start_pos_.y ||
            cur_end.x   != cached_end_pos_.x   || cur_end.y   != cached_end_pos_.y) {
            dirty_ = true;
        }
    }
    if (!dirty_) return;

    // Rebuild polyline
    cached_polyline_.clear();
    if (opt_start) cached_polyline_.push_back(cur_start);
    for (const auto& c : children()) {
        cached_polyline_.push_back(c->worldPos());
    }
    if (opt_end) cached_polyline_.push_back(cur_end);

    cached_start_pos_ = cur_start;
    cached_end_pos_   = cur_end;

    // Derive bounding box
    if (cached_polyline_.empty()) {
        cached_min_ = Pt(0, 0);
        cached_max_ = Pt(0, 0);
    } else {
        float min_x = cached_polyline_[0].x, min_y = cached_polyline_[0].y;
        float max_x = min_x, max_y = min_y;
        for (size_t i = 1; i < cached_polyline_.size(); ++i) {
            min_x = std::min(min_x, cached_polyline_[i].x);
            min_y = std::min(min_y, cached_polyline_[i].y);
            max_x = std::max(max_x, cached_polyline_[i].x);
            max_y = std::max(max_y, cached_polyline_[i].y);
        }
        cached_min_ = Pt(min_x - BBOX_PADDING, min_y - BBOX_PADDING);
        cached_max_ = Pt(max_x + BBOX_PADDING, max_y + BBOX_PADDING);
    }

    dirty_ = false;
}

RoutingPoint* Wire::addRoutingPoint(Pt pos, size_t index) {
    auto rp = std::make_unique<RoutingPoint>(pos);
    auto* ptr = rp.get();

    if (index >= children().size()) {
        addChild(std::move(rp));
    } else {
        // Insert at specific position: remove tail, add new, re-add tail
        std::vector<std::unique_ptr<Widget>> tail;
        while (children().size() > index) {
            auto child = removeChild(children().back().get());
            tail.push_back(std::unique_ptr<Widget>(static_cast<Widget*>(child.release())));
        }
        addChild(std::move(rp));
        for (auto it = tail.rbegin(); it != tail.rend(); ++it) {
            addChild(std::move(*it));
        }
    }

    invalidateGeometry();

    // Register the new routing point in the scene (Grid + id index)
    // so it is discoverable by hit testing.
    if (scene()) {
        scene()->attachToScene(ptr);
        // Also update the Wire's own Grid entry (bounds changed)
        if (isClickable()) {
            scene()->grid().update(this);
        }
    }

    return ptr;
}

void Wire::removeRoutingPoint(size_t index) {
    if (index < children().size()) {
        auto* rp = children()[index].get();

        // Detach from scene (Grid + id index) before destroying
        if (scene()) {
            scene()->detachFromScene(rp);
        }

        removeChild(rp);

        invalidateGeometry();

        // Update the Wire's own Grid entry (bounds changed)
        if (scene() && isClickable()) {
            scene()->grid().update(this);
        }
    }
}

void Wire::render(IDrawList* dl, const RenderContext& ctx) const {
    if (!dl) return;
    const auto& world_pts = polyline();
    if (world_pts.size() < 2) return;

    // Selection/hover/energized styling
    uint32_t color = render_theme::COLOR_WIRE_UNSEL;
    float thickness = WIRE_THICKNESS * ctx.zoom;
    if (ctx.selected_wire == this) {
        color = render_theme::COLOR_WIRE;
        thickness = 2.5f * ctx.zoom;
    } else if (ctx.hovered_wire == this) {
        color = render_theme::COLOR_WIRE_HOVER;
        thickness = 2.0f * ctx.zoom;
    } else if (ctx.energized_wires &&
               ctx.energized_wires->count(id_) > 0) {
        color = render_theme::COLOR_WIRE_CURRENT;
        thickness = 2.0f * ctx.zoom;
    }

    if (crossings_.empty()) {
        // Fast path: no crossings — single polyline
        static thread_local std::vector<Pt> screen_pts;
        screen_pts.resize(world_pts.size());
        for (size_t i = 0; i < world_pts.size(); ++i)
            screen_pts[i] = ctx.world_to_screen(world_pts[i]);
        dl->add_polyline(screen_pts.data(), screen_pts.size(), color, thickness);
    } else {
        // Classify crossings by segment index + parametric t
        struct CrossOnSeg {
            size_t seg_idx;
            float t;
            Pt pos;
            SegDir my_seg_dir;
        };
        ui::SmallVector<CrossOnSeg, 8> segs;
        for (const auto& c : crossings_) {
            if (!c.draw_arc) continue;

            for (size_t i = 0; i + 1 < world_pts.size(); ++i) {
                Pt a = world_pts[i], b = world_pts[i + 1];
                float seg_len_sq = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
                if (seg_len_sq < 1e-6f) continue;
                float t = ((c.pos.x - a.x) * (b.x - a.x) + (c.pos.y - a.y) * (b.y - a.y)) / seg_len_sq;
                if (t >= -0.01f && t <= 1.01f) {
                    Pt proj(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
                    float dist_sq = (proj.x - c.pos.x) * (proj.x - c.pos.x) +
                                    (proj.y - c.pos.y) * (proj.y - c.pos.y);
                    if (dist_sq < 1.0f) {
                        segs.push_back({i, std::max(0.0f, std::min(1.0f, t)), c.pos, c.my_seg_dir});
                        break;
                    }
                }
            }
        }

        std::sort(segs.begin(), segs.end(), [](const CrossOnSeg& a, const CrossOnSeg& b) {
            return a.seg_idx < b.seg_idx || (a.seg_idx == b.seg_idx && a.t < b.t);
        });

        // Draw polyline with gaps at crossings
        float gap_r = render_theme::ARC_RADIUS_WORLD;
        ui::SmallVector<Pt, 16> current_sub;
        size_t cross_i = 0;

        for (size_t seg = 0; seg + 1 < world_pts.size(); ++seg) {
            Pt a = world_pts[seg], b = world_pts[seg + 1];
            float seg_len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));

            ui::SmallVector<float, 4> seg_ts;
            while (cross_i < segs.size() && segs[cross_i].seg_idx == seg) {
                seg_ts.push_back(segs[cross_i].t);
                cross_i++;
            }

            if (seg_ts.empty()) {
                if (current_sub.empty())
                    current_sub.push_back(ctx.world_to_screen(a));
                current_sub.push_back(ctx.world_to_screen(b));
            } else {
                if (current_sub.empty())
                    current_sub.push_back(ctx.world_to_screen(a));

                float last_gap_end_t = -1.0f;
                for (float ct : seg_ts) {
                    float gap_t = (seg_len > 1e-3f) ? gap_r / seg_len : 0.5f;
                    float t_before = ct - gap_t;
                    float t_after  = ct + gap_t;

                    if (t_before < last_gap_end_t) {
                        last_gap_end_t = std::max(last_gap_end_t, t_after);
                        continue;
                    }

                    if (t_before > 0.001f) {
                        Pt p_before(a.x + t_before * (b.x - a.x), a.y + t_before * (b.y - a.y));
                        current_sub.push_back(ctx.world_to_screen(p_before));
                    }

                    if (current_sub.size() >= 2)
                        dl->add_polyline(current_sub.data(), current_sub.size(), color, thickness);
                    current_sub.clear();

                    if (t_after < 0.999f) {
                        Pt p_after(a.x + t_after * (b.x - a.x), a.y + t_after * (b.y - a.y));
                        current_sub.push_back(ctx.world_to_screen(p_after));
                    }
                    last_gap_end_t = t_after;
                }

                if (current_sub.empty() && last_gap_end_t > 0.0f) {
                    if (last_gap_end_t < 1.0f) {
                        Pt p(a.x + last_gap_end_t * (b.x - a.x), a.y + last_gap_end_t * (b.y - a.y));
                        current_sub.push_back(ctx.world_to_screen(p));
                    }
                }
                current_sub.push_back(ctx.world_to_screen(b));
            }
        }

        if (current_sub.size() >= 2)
            dl->add_polyline(current_sub.data(), current_sub.size(), color, thickness);

        // Jump arcs at crossings
        for (const auto& crossing : crossings_) {
            if (!crossing.draw_arc) continue;

            Pt screen_cross = ctx.world_to_screen(crossing.pos);
            float arc_radius = render_theme::ARC_RADIUS_WORLD * ctx.zoom;

            bool arc_vertical = (crossing.my_seg_dir == SegDir::Horiz ||
                                 crossing.my_seg_dir == SegDir::Unknown);

            Pt arc_points[render_theme::ARC_SEGMENTS + 1];
            for (int i = 0; i <= render_theme::ARC_SEGMENTS; ++i) {
                float angle = 3.14159265f * static_cast<float>(i) / render_theme::ARC_SEGMENTS;
                if (arc_vertical) {
                    arc_points[i] = Pt(screen_cross.x + std::cos(angle) * arc_radius,
                                       screen_cross.y - std::sin(angle) * arc_radius);
                } else {
                    arc_points[i] = Pt(screen_cross.x + std::sin(angle) * arc_radius,
                                       screen_cross.y + std::cos(angle) * arc_radius);
                }
            }
            dl->add_polyline(arc_points, render_theme::ARC_SEGMENTS + 1, color, thickness);
        }
    }

    // Draw routing point handles when wire is selected or hovered
    if (ctx.selected_wire == this || ctx.hovered_wire == this) {
        float rp_radius = 4.0f * ctx.zoom;
        for (const auto& child : children()) {
            Pt screen_rp = ctx.world_to_screen(child->worldPos());
            uint32_t rp_color = render_theme::COLOR_ROUTING_POINT;
            if (ctx.hovered_routing_point == child.get()) {
                rp_color = render_theme::COLOR_WIRE_HOVER;
            }
            handle_renderer::draw_handle(*dl, screen_rp, rp_radius, rp_color);
        }
    }
}

void compute_wire_crossings(Scene& scene) {
    // Collect all wires and clear their crossings for this frame.
    ui::SmallVector<Wire*, 64> wires;
    for (const auto& r : scene.roots()) {
        if (static_cast<Widget*>(r.get())->renderLayer() == RenderLayer::Wire) {
            if (auto* w = dynamic_cast<Wire*>(r.get())) {
                w->clearCrossings();
                wires.push_back(w);
            }
        }
    }

    if (wires.size() < 2) return;

    // Build pointer -> index map for stable arc/gap ordering.
    std::unordered_map<Wire*, size_t> wire_index;
    wire_index.reserve(wires.size());
    for (size_t i = 0; i < wires.size(); ++i) {
        wire_index[wires[i]] = i;
    }

    // Broadphase via spatial Grid
    struct PairHash {
        size_t operator()(std::pair<size_t, size_t> p) const {
            return std::hash<size_t>()(p.first) ^ (std::hash<size_t>()(p.second) * 2654435761u);
        }
    };
    std::unordered_set<std::pair<size_t, size_t>, PairHash> checked;

    scene.grid().forEachCell([&](const std::vector<ui::Widget*>& cell_widgets) {
        ui::SmallVector<Wire*, 8> cell_wires;
        for (auto* widget : cell_widgets) {
            auto it = wire_index.find(static_cast<Wire*>(widget));
            if (it != wire_index.end()) {
                cell_wires.push_back(static_cast<Wire*>(widget));
            }
        }

        for (size_t a = 0; a < cell_wires.size(); ++a) {
            for (size_t b = a + 1; b < cell_wires.size(); ++b) {
                size_t idx_a = wire_index[cell_wires[a]];
                size_t idx_b = wire_index[cell_wires[b]];
                size_t lo = idx_a < idx_b ? idx_a : idx_b;
                size_t hi = idx_a < idx_b ? idx_b : idx_a;

                if (!checked.emplace(lo, hi).second) continue;

                Wire* w_lo = wires[lo];
                Wire* w_hi = wires[hi];
                const auto& poly_lo = w_lo->polyline();
                const auto& poly_hi = w_hi->polyline();
                if (poly_lo.size() < 2 || poly_hi.size() < 2) continue;

                for (size_t i = 0; i + 1 < poly_lo.size(); ++i) {
                    for (size_t j = 0; j + 1 < poly_hi.size(); ++j) {
                        auto pt = segment_crosses(poly_lo[i], poly_lo[i + 1],
                                                  poly_hi[j], poly_hi[j + 1]);
                        if (pt) {
                            SegDir lo_dir = segment_direction(poly_lo[i], poly_lo[i + 1]);
                            w_lo->appendCrossing({*pt, lo_dir, false});

                            SegDir hi_dir = segment_direction(poly_hi[j], poly_hi[j + 1]);
                            w_hi->appendCrossing({*pt, hi_dir, true});
                        }
                    }
                }
            }
        }
    });
}

} // namespace visual

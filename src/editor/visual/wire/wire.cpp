#include "visual/wire/wire.h"
#include "visual/wire/wire_end.h"
#include "visual/wire/routing_point.h"
#include "visual/scene.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/render_context.h"
#include "router/crossings.h"
#include <algorithm>
#include <cmath>

namespace visual {

Wire::Wire(const std::string& id, WireEnd* start, WireEnd* end)
    : id_(id), start_(start), end_(end)
{
    if (start_) start_->setWire(this);
    if (end_) end_->setWire(this);
}

Wire::~Wire() {
    if (start_) start_->clearWire();
    if (end_) end_->clearWire();
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
    // Auto-detect endpoint movement: if the WireEnd world positions
    // changed since last rebuild, treat the cache as dirty.
    Pt cur_start = start_ ? start_->worldPos() : Pt(0, 0);
    Pt cur_end   = end_   ? end_->worldPos()   : Pt(0, 0);
    if (!dirty_) {
        if (cur_start.x != cached_start_pos_.x || cur_start.y != cached_start_pos_.y ||
            cur_end.x   != cached_end_pos_.x   || cur_end.y   != cached_end_pos_.y) {
            dirty_ = true;
        }
    }
    if (!dirty_) return;

    // Rebuild polyline
    cached_polyline_.clear();
    if (start_) cached_polyline_.push_back(cur_start);
    for (const auto& c : children()) {
        cached_polyline_.push_back(c->worldPos());
    }
    if (end_) cached_polyline_.push_back(cur_end);

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

    // Update Grid entry if in scene (bounds changed)
    if (scene() && isClickable()) {
        scene()->grid().update(this);
    }

    return ptr;
}

void Wire::removeRoutingPoint(size_t index) {
    if (index < children().size()) {
        removeChild(children()[index].get());

        invalidateGeometry();

        // Update Grid entry if in scene (bounds changed)
        if (scene() && isClickable()) {
            scene()->grid().update(this);
        }
    }
}

void Wire::onEndpointDestroyed(WireEnd* end) {
    if (end == start_) start_ = nullptr;
    if (end == end_) end_ = nullptr;

    invalidateGeometry();

    if (scene()) scene()->remove(this);
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
               ctx.energized_wires->count(std::string(id_)) > 0) {
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
        std::vector<CrossOnSeg> segs;
        for (const auto& c : crossings_) {
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
        std::vector<Pt> current_sub;
        size_t cross_i = 0;

        for (size_t seg = 0; seg + 1 < world_pts.size(); ++seg) {
            Pt a = world_pts[seg], b = world_pts[seg + 1];
            float seg_len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));

            std::vector<float> seg_ts;
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
                for (float ct : seg_ts) {
                    float gap_t = (seg_len > 1e-3f) ? gap_r / seg_len : 0.5f;
                    float t_before = ct - gap_t;
                    float t_after  = ct + gap_t;

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
                }

                if (current_sub.empty()) {
                    float gap_t = (seg_len > 1e-3f) ? gap_r / seg_len : 0.5f;
                    float last_after = seg_ts.back() + gap_t;
                    if (last_after < 1.0f) {
                        Pt p(a.x + last_after * (b.x - a.x), a.y + last_after * (b.y - a.y));
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
    std::vector<Wire*> wires;
    std::vector<std::vector<Pt>> polylines;

    for (const auto& r : scene.roots()) {
        if (r->renderLayer() == RenderLayer::Wire) {
            if (auto* w = dynamic_cast<Wire*>(r.get())) {
                wires.push_back(w);
                polylines.push_back(w->polyline());
            }
        }
    }

    for (size_t i = 0; i < wires.size(); ++i) {
        wires[i]->setCrossings(find_wire_crossings(i, polylines));
    }
}

} // namespace visual

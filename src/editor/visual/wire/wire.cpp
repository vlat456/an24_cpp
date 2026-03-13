#include "visual/wire/wire.h"
#include "visual/wire/wire_end.h"
#include "visual/wire/routing_point.h"
#include "visual/scene.h"
#include "visual/renderer/draw_list.h"
#include "visual/renderer/render_theme.h"
#include "visual/renderer/handle_renderer.h"
#include "visual/render_context.h"
#include <algorithm>

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
            tail.push_back(removeChild(children().back().get()));
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

    // Re-use a thread-local buffer for screen-space points to avoid
    // allocating a new vector every frame per wire.
    static thread_local std::vector<Pt> screen_pts;
    screen_pts.resize(world_pts.size());
    for (size_t i = 0; i < world_pts.size(); ++i) {
        screen_pts[i] = ctx.world_to_screen(world_pts[i]);
    }

    // Selection/hover/energized styling (using theme colors)
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

    dl->add_polyline(screen_pts.data(), screen_pts.size(), color, thickness);

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

} // namespace visual

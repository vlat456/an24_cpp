#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "viewport/viewport.h"
#include <algorithm>
#include <unordered_set>

void CanvasInput::handle_drag_node(Pt world_delta) {
    drag_anchor_ = drag_anchor_ + world_delta;

    Pt const snapped = editor_math::snap_to_grid(drag_anchor_, viewport_.grid_step);

    std::unordered_set<core::InternedId> connected_wire_ids;

    drag_current_positions_.clear();
    size_t drag_idx = 0;
    for (const auto& node_id : selected_node_ids()) {
        auto* widget = resolve_node(node_id);
        if (!widget) continue;
        Pt const offset = (drag_idx < drag_offsets_.size()) ? drag_offsets_[drag_idx] : Pt(0, 0);
        Pt const new_pos = snapped + offset;

        // Track final positions for commit (no widget readback needed).
        drag_current_positions_.push_back(new_pos);

        widget->setLocalPos(new_pos);

        if (!node_id.empty()) {
            for (const bp2::Blueprint::Wire& w : host_->wires()) {
                auto src_node = w.source.node;
                auto tgt_node = w.target.node;
                if (src_node == node_id || tgt_node == node_id) {
                    connected_wire_ids.insert(w.id);
                }
            }
        }

        if (widget->isClickable())
            scene_.grid().update(widget);
        ++drag_idx;
    }

    for (auto wid : connected_wire_ids) {
        auto* found = scene_.find(interner_->resolve(wid));
        auto* wire = (found && found->kind() == ui::WidgetKind::Wire)
                     ? static_cast<visual::Wire*>(found) : nullptr;
        if (wire) {
            wire->invalidateGeometry();
            if (wire->isClickable()) {
                scene_.grid().update(wire);
            }
        }
    }
}

void CanvasInput::handle_resize_node(Pt world_delta) {
    drag_anchor_ = drag_anchor_ + world_delta;
    auto* resize_widget = resolve_node(resize_widget_id_);
    if (!resize_widget) return;

    float const grid = viewport_.grid_step;
    Pt const orig_pos = resize_original_pos_;
    Pt const orig_sz = resize_original_size_;
    Pt const delta = drag_anchor_;
    Pt new_pos = orig_pos;
    Pt new_size = orig_sz;

    switch (resize_corner_) {
        case ResizeCorner::BottomRight:
            new_size = Pt(orig_sz.x + delta.x, orig_sz.y + delta.y);
            break;
        case ResizeCorner::BottomLeft:
            new_pos.x = orig_pos.x + delta.x;
            new_size = Pt(orig_sz.x - delta.x, orig_sz.y + delta.y);
            break;
        case ResizeCorner::TopRight:
            new_pos.y = orig_pos.y + delta.y;
            new_size = Pt(orig_sz.x + delta.x, orig_sz.y - delta.y);
            break;
        case ResizeCorner::TopLeft:
            new_pos = Pt(orig_pos.x + delta.x, orig_pos.y + delta.y);
            new_size = Pt(orig_sz.x - delta.x, orig_sz.y - delta.y);
            break;
    }

    float min_w = editor_constants::PORT_LAYOUT_GRID;
    float min_h = editor_constants::PORT_LAYOUT_GRID;
    if (resize_widget->kind() == ui::WidgetKind::Node) {
        auto* node_widget = static_cast<visual::NodeWidget*>(resize_widget);
        Pt const minimum = node_widget->minimumNodeSize();
        min_w = minimum.x;
        min_h = minimum.y;
    }
    if (new_size.x < min_w) {
        if (resize_corner_ == ResizeCorner::TopLeft || resize_corner_ == ResizeCorner::BottomLeft)
            new_pos.x = orig_pos.x + orig_sz.x - min_w;
        new_size.x = min_w;
    }
    if (new_size.y < min_h) {
        if (resize_corner_ == ResizeCorner::TopLeft || resize_corner_ == ResizeCorner::TopRight)
            new_pos.y = orig_pos.y + orig_sz.y - min_h;
        new_size.y = min_h;
    }

    new_pos = editor_math::snap_to_grid(new_pos, grid);
    new_size = editor_math::snap_to_grid(new_size, grid);

    // Resizing must never violate the node's required minimum, even when the
    // user-visible grid step differs from the layout grid and snap_to_grid()
    // rounds down after clamping.
    if (new_size.x < min_w) {
        if (resize_corner_ == ResizeCorner::TopLeft || resize_corner_ == ResizeCorner::BottomLeft) {
            new_pos.x = orig_pos.x + orig_sz.x - min_w;
            new_pos.x = std::round(new_pos.x / grid) * grid;
        }
        new_size.x = min_w;
    }
    if (new_size.y < min_h) {
        if (resize_corner_ == ResizeCorner::TopLeft || resize_corner_ == ResizeCorner::TopRight) {
            new_pos.y = orig_pos.y + orig_sz.y - min_h;
            new_pos.y = std::round(new_pos.y / grid) * grid;
        }
        new_size.y = min_h;
    }

    resize_widget->setLocalPos(new_pos);
    resize_widget->layout(new_size.x, new_size.y);

    // Track arithmetic final state for commit (no widget readback needed).
    resize_current_pos_ = new_pos;
    resize_current_size_ = new_size;
}

InputResult CanvasInput::on_mouse_drag(MouseButton btn, Pt screen_delta, Pt canvas_min) {
    InputResult result;
    float const zoom = viewport_.zoom;
    Pt const world_delta(screen_delta.x / zoom, screen_delta.y / zoom);

    if (btn == MouseButton::Left) {
        switch (state_) {
            case InputState::Panning:
                viewport_.pan.x -= world_delta.x;
                viewport_.pan.y -= world_delta.y;
                advance_world_cursor(world_delta);
                break;

            case InputState::DraggingNode:
                handle_drag_node(world_delta);
                break;

            case InputState::DraggingRoutingPoint: {
                drag_anchor_ = drag_anchor_ + world_delta;
                Pt const snapped = editor_math::snap_to_grid(drag_anchor_, viewport_.grid_step);
                rp_drag_pos_ = snapped;

                auto* rp_wire = resolve_wire(rp_wire_id_);
                visual::RoutingPoint* rp_point = nullptr;
                if (rp_wire && rp_index_ < rp_wire->children().size()) {
                    auto* rp_child = rp_wire->children()[rp_index_].get();
                    visual::RoutingPoint const* rp_point =
                        (rp_child->kind() == ui::WidgetKind::RoutingPoint)
                        ? static_cast<visual::RoutingPoint*>(rp_child) : nullptr;
                }

                if (rp_point) {
                    rp_point->setLocalPos(snapped);
                    if (rp_wire) {
                        rp_wire->invalidateGeometry();
                        if (rp_wire->isClickable()) {
                            scene_.grid().update(rp_wire);
                        }
                    }
                    if (rp_point->scene())
                        rp_point->scene()->grid().update(rp_point);
                }
                break;
            }

            case InputState::CreatingWire:
            case InputState::ReconnectingWire:
                advance_world_cursor(world_delta);
                break;

            case InputState::MarqueeSelect:
                marquee_end_ = marquee_end_ + world_delta;
                break;

            case InputState::ResizingNode:
                handle_resize_node(world_delta);
                break;

            case InputState::DraggingSlider:
            case InputState::DraggingKnob: {
                advance_world_cursor(world_delta);
                Pt semantic_point = last_world_pos_;
                if (semantic_session_seed_) {
                    semantic_point = Pt(last_world_pos_.x - semantic_session_seed_->node_world_pos.x,
                                        last_world_pos_.y - semantic_session_seed_->node_world_pos.y);
                }
                editor::presentation::SemanticCanvasControllerResult const semantic =
                    semantic_canvas_controller_.on_pointer_drag(semantic_point);
                publish_semantic_control_result(semantic, result);
                break;
            }

            case InputState::Idle:
                break;
        }
    }
    return result;
}

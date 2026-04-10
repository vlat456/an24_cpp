#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_hittest.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "visual/widgets/content_widgets.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "viewport/viewport.h"
#include "canvas_input_internal.h"
#include <algorithm>
#include <unordered_set>

using namespace canvas_input_impl;

void CanvasInput::handle_drag_node(Pt world_delta) {
    drag_anchor_ = drag_anchor_ + world_delta;

    bool all_ref_nodes = true;
    ui::InternedId value_type = interner_.intern("Value");
    for (auto* w : selected_nodes()) {
        ui::InternedId nid = interner_.intern(w->id());
        if (nid.empty()) continue;
        const bp2::Blueprint::Node* n = host_.find_node(nid);
        if (!n || n->view.render_hint != "ref" || n->semantic.type == value_type) {
            all_ref_nodes = false;
            break;
        }
    }
    Pt snapped = all_ref_nodes
        ? editor_math::snap_to_grid(drag_anchor_, viewport_.grid_step)
        : editor_math::snap_to_half_grid(drag_anchor_, viewport_.grid_step);

    auto nodes = selected_nodes();

    std::unordered_set<ui::InternedId> connected_wire_ids;

    for (size_t i = 0; i < nodes.size(); i++) {
        auto* widget = nodes[i];
        Pt offset = (i < drag_offsets_.size()) ? drag_offsets_[i] : Pt(0, 0);
        Pt new_pos = snapped + offset;

        widget->setLocalPos(new_pos);

        ui::InternedId node_iid = interner_.intern(widget->id());
        if (!node_iid.empty()) {
            for (const bp2::Blueprint::Wire& w : host_.wires()) {
                auto src_node = w.source.node;
                auto tgt_node = w.target.node;
                if (src_node == node_iid || tgt_node == node_iid) {
                    connected_wire_ids.insert(w.id);
                }
            }
        }

        if (widget->isClickable())
            scene_.grid().update(widget);
    }

    for (auto wid : connected_wire_ids) {
        auto* wire = dynamic_cast<visual::Wire*>(
            scene_.find(interner_.resolve(wid)));
        if (wire) {
            wire->invalidateGeometry();
            if (wire->isClickable())
                scene_.grid().update(wire);
        }
    }
}

void CanvasInput::handle_resize_node(Pt world_delta) {
    drag_anchor_ = drag_anchor_ + world_delta;
    auto* resize_widget = resolve_node(resize_widget_id_);
    if (!resize_widget) return;

    float grid = viewport_.grid_step;
    Pt orig_pos = resize_original_pos_;
    Pt orig_sz = resize_original_size_;
    Pt delta = drag_anchor_;
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

    resize_widget->setLocalPos(new_pos);
    resize_widget->layout(new_size.x, new_size.y);
}

InputResult CanvasInput::on_mouse_drag(MouseButton btn, Pt screen_delta, Pt canvas_min) {
    InputResult result;
    float zoom = viewport_.zoom;
    Pt world_delta(screen_delta.x / zoom, screen_delta.y / zoom);

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
                Pt snapped = editor_math::snap_to_grid(drag_anchor_, viewport_.grid_step);

                if (rp_point_) {
                    rp_point_->setLocalPos(snapped);
                    auto* rp_wire = resolve_wire(rp_wire_id_);
                    if (rp_wire) {
                        rp_wire->invalidateGeometry();
                        if (rp_wire->scene() && rp_wire->isClickable())
                            rp_wire->scene()->grid().update(rp_wire);
                    }
                    if (rp_point_->scene())
                        rp_point_->scene()->grid().update(rp_point_);
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

            case InputState::DraggingSlider: {
                advance_world_cursor(world_delta);
                float local_x = last_world_pos_.x - slider_widget_world_pos_.x;
                float pad = visual::SliderWidget::HANDLE_RADIUS;
                float track_w = slider_widget_width_ - 2.0f * pad;
                float t = (track_w > 0.0f) ? std::clamp((local_x - pad) / track_w, 0.0f, 1.0f) : 0.0f;

                const bp2::Blueprint::Node* node = host_.find_node(slider_node_id_);
                if (node) {
                    float val = node->view.content_min + t * (node->view.content_max - node->view.content_min);
                    result.slider_node_id = std::string(interner_.resolve(slider_node_id_));
                    result.slider_value = val;
                }
                break;
            }

            case InputState::DraggingKnob: {
                advance_world_cursor(world_delta);
                float dx = last_world_pos_.x - knob_drag_start_x_;
                constexpr float PIXELS_PER_STEP = 30.0f;
                int delta_steps = static_cast<int>(dx / PIXELS_PER_STEP);
                int new_pos = std::clamp(knob_drag_start_pos_ + delta_steps,
                                          0, knob_num_positions_ - 1);

                result.knob_node_id = std::string(interner_.resolve(knob_node_id_));
                result.knob_position = new_pos;
                break;
            }

            case InputState::Idle:
                break;
        }
    }
    return result;
}

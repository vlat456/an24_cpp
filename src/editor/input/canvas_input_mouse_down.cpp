#include "input/canvas_input.h"
#include "visual/scene.h"
#include "visual/scene_hittest.h"
#include "visual/snap.h"
#include "visual/node/visual_node.h"
#include "visual/widgets/content_widgets.h"
#include "visual/port/visual_port.h"
#include "visual/wire/wire.h"
#include "visual/wire/routing_point.h"
#include "viewport/viewport.h"
#include "canvas_input_internal.h"

using namespace canvas_input_impl;

InputResult CanvasInput::on_mouse_down(Pt screen_pos, MouseButton btn, Pt canvas_min, Modifiers mods) {
    InputResult result;
    Pt world = viewport_.screen_to_world(screen_pos, canvas_min);
    last_world_pos_ = world;

    if (btn == MouseButton::Left) {
        if (mods.shift) {
            auto hit = visual::hit_test(scene_, world);
            if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
                result.toggle_probe_wire_id = std::string(hw->wire->id());
                result.has_toggle_probe_world_pos = true;
                result.toggle_probe_world_pos = world;
                return result;
            }
        }

        if (read_only) {
            auto hit = visual::hit_test(scene_, world);
            if (auto* h = std::get_if<visual::HitNode>(&hit)) {
                if (!mods.ctrl) clear_selection();
                add_node_selection(h->widget);
            } else {
                clear_selection();
                enter_panning();
            }
            return result;
        }

        if (simulation_mode) {
            auto hit = visual::hit_test(scene_, world);
            if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
                float slider_local_x = 0.0f;
                auto slider_id = check_slider_hit(*hn->widget, world, slider_local_x);
                if (!slider_id.empty()) {
                    auto* nw = dynamic_cast<visual::NodeWidget*>(hn->widget);
                    if (nw) {
                        Bounds cb = nw->contentBounds();
                        Pt nw_pos = nw->worldPos();
                        Pt slider_wpos(nw_pos.x + cb.x, nw_pos.y + cb.y);
                        enter_drag_slider(hn->widget, slider_wpos, cb.w);

                        ui::InternedId nid_iid = interner_.lookup(slider_id);
                        const bp2::Blueprint::Node* node = nid_iid.empty() ? nullptr
                                                                           : model_.current().find_node(nid_iid);
                        if (node) {
                            float pad = visual::SliderWidget::HANDLE_RADIUS;
                            float track_w = cb.w - 2.0f * pad;
                            float t = (track_w > 0.0f) ? std::clamp((slider_local_x - pad) / track_w, 0.0f, 1.0f) : 0.0f;
                            float val = node->content_min + t * (node->content_max - node->content_min);
                            result.slider_node_id = std::move(slider_id);
                            result.slider_value = val;
                        }
                    }
                    return result;
                }

                auto knob_id = check_knob_hit(*hn->widget, world);
                if (!knob_id.empty()) {
                    enter_drag_knob(hn->widget, world);
                    result.knob_node_id = std::move(knob_id);
                    result.knob_position = knob_drag_start_pos_;
                    return result;
                }

                auto toggle_id = check_content_toggle(*hn->widget, world);
                if (!toggle_id.empty()) {
                    result.toggle_switch_node_id = std::move(toggle_id);
                    return result;
                }
            }
            clear_selection();
            enter_panning();
            return result;
        }

        auto port_hit = visual::hit_test_ports(scene_, world);
        if (auto* ph = std::get_if<visual::HitPort>(&port_hit)) {
            auto wire_match = find_wire_on_port(ph->port);
            if (wire_match) {
                enter_reconnect_wire(wire_match->wire_index, wire_match->detach_start,
                                     wire_match->anchor_pos, wire_match->fixed_side,
                                     wire_match->fixed_type);
                return result;
            }
            Pt port_center = ph->port->worldPos() + Pt(visual::PortConstants::RADIUS, visual::PortConstants::RADIUS);
            enter_create_wire(ph->port, port_center);
            return result;
        }

        auto hit = visual::hit_test(scene_, world);

        if (mods.alt) {
            enter_marquee(world);
        } else if (auto* hrh = std::get_if<visual::HitResizeHandle>(&hit)) {
            enter_resize_node(hrh->widget, hrh->corner);
        } else if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
            float slider_local_x = 0.0f;
            auto slider_id = check_slider_hit(*hn->widget, world, slider_local_x);
            if (!slider_id.empty()) {
                auto* nw = dynamic_cast<visual::NodeWidget*>(hn->widget);
                if (nw) {
                    Bounds cb = nw->contentBounds();
                    Pt nw_pos = nw->worldPos();
                    Pt slider_wpos(nw_pos.x + cb.x, nw_pos.y + cb.y);
                    enter_drag_slider(hn->widget, slider_wpos, cb.w);

                    ui::InternedId nid_iid = interner_.lookup(slider_id);
                    const bp2::Blueprint::Node* node = nid_iid.empty() ? nullptr
                                                                       : model_.current().find_node(nid_iid);
                    if (node) {
                        float pad = visual::SliderWidget::HANDLE_RADIUS;
                        float track_w = cb.w - 2.0f * pad;
                        float t = (track_w > 0.0f) ? std::clamp((slider_local_x - pad) / track_w, 0.0f, 1.0f) : 0.0f;
                        float val = node->content_min + t * (node->content_max - node->content_min);
                        result.slider_node_id = std::move(slider_id);
                        result.slider_value = val;
                    }
                }
                return result;
            }

            auto knob_id = check_knob_hit(*hn->widget, world);
            if (!knob_id.empty()) {
                enter_drag_knob(hn->widget, world);
                result.knob_node_id = std::move(knob_id);
                result.knob_position = knob_drag_start_pos_;
                return result;
            }

            auto toggle_id = check_content_toggle(*hn->widget, world);
            if (!toggle_id.empty()) {
                result.toggle_switch_node_id = std::move(toggle_id);
                return result;
            }
            enter_drag_node(hn->widget, false, mods.ctrl);
        } else if (auto* hrp = std::get_if<visual::HitRoutingPoint>(&hit)) {
            enter_drag_routing_point(hrp->wire, hrp->point, hrp->index);
        } else if (auto* hw = std::get_if<visual::HitWire>(&hit)) {
            clear_selection();
            selected_wire_id_ = interner_.intern(hw->wire->id());
        } else {
            clear_selection();
            enter_panning();
        }
    } else if (btn == MouseButton::Right && !read_only && !simulation_mode) {
        auto hit = visual::hit_test(scene_, world);
        if (auto* hn = std::get_if<visual::HitNode>(&hit)) {
            result.show_node_context_menu = true;
            result.context_menu_node_id = std::string(hn->widget->id());
        } else if (std::holds_alternative<visual::HitEmpty>(hit)) {
            result.show_context_menu = true;
            result.context_menu_pos = world;
        }
    }
    return result;
}
